// SPDX-License-Identifier: MIT

#include "plugin_process.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <sddl.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include "liquid/frontend_abi.h"

namespace liquify::detail {
namespace {

namespace protocol = plugin_protocol;
using Clock = std::chrono::steady_clock;

auto process_error(liquid::errc code, std::string message, std::string context = {})
    -> liquid::error {
  return {code, std::move(message), std::move(context)};
}

auto remaining(Clock::time_point deadline) -> std::chrono::milliseconds {
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
  return std::max(left, std::chrono::milliseconds(0));
}

auto status_code(std::uint32_t status) -> liquid::errc {
  switch (status) {
  case LIQUID_FRONTEND_STATUS_INVALID_ARGUMENT:
    return liquid::errc::invalid_argument;
  case LIQUID_FRONTEND_STATUS_UNSUPPORTED:
    return liquid::errc::unsupported;
  case LIQUID_FRONTEND_STATUS_CANCELLED:
    return liquid::errc::unavailable;
  default:
    return liquid::errc::internal;
  }
}

} // namespace

#if defined(_WIN32)

auto plugin_host_executable() -> std::filesystem::path {
  constexpr auto name = L"liquify-plugin-host.exe";
  auto buffer = std::wstring(MAX_PATH, L'\0');
  for (;;) {
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0) {
      break;
    }
    if (size < buffer.size()) {
      buffer.resize(size);
      const auto adjacent = std::filesystem::path(buffer).parent_path() / name;
      if (std::filesystem::exists(adjacent)) {
        return adjacent;
      }
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
#if defined(LIQUIFY_PLUGIN_HOST)
  return std::filesystem::path(LIQUIFY_PLUGIN_HOST);
#else
  return name;
#endif
}

struct PluginProcess::Child {
  HANDLE process = nullptr;
  HANDLE job = nullptr;
  HANDLE write = nullptr; // IDE -> host
  HANDLE read = nullptr;  // host -> IDE

  Child() = default;
  Child(const Child &) = delete;
  auto operator=(const Child &) -> Child & = delete;

  ~Child() {
    if (write != nullptr) {
      CloseHandle(write);
    }
    if (process != nullptr && WaitForSingleObject(process, 1000) != WAIT_OBJECT_0) {
      TerminateProcess(process, 1);
      WaitForSingleObject(process, INFINITE);
    }
    if (read != nullptr) {
      CloseHandle(read);
    }
    if (job != nullptr) {
      CloseHandle(job); // kill on close: nothing the host started outlives it
    }
    if (process != nullptr) {
      CloseHandle(process);
    }
  }

  [[nodiscard]] auto exit_text() const -> std::string {
    if (process == nullptr || WaitForSingleObject(process, 100) != WAIT_OBJECT_0) {
      return "plugin host closed its channel";
    }
    auto code = DWORD{};
    GetExitCodeProcess(process, &code);
    char text[64];
    std::snprintf(text, sizeof(text), "plugin host exited with status 0x%lX",
                  static_cast<unsigned long>(code));
    return text;
  }

  void kill() const {
    if (process != nullptr) {
      TerminateProcess(process, 1);
      WaitForSingleObject(process, INFINITE);
    }
  }

  [[nodiscard]] auto send(std::span<const std::byte> bytes, Clock::time_point)
      -> liquid::result<void> {
    // Anonymous pipes have no timed write; the 1 MiB pipe buffer holds every ordinary frame.
    while (!bytes.empty()) {
      auto written = DWORD{};
      const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1U << 20U));
      if (!WriteFile(write, bytes.data(), chunk, &written, nullptr) || written == 0) {
        return std::unexpected(process_error(liquid::errc::unavailable, exit_text()));
      }
      bytes = bytes.subspan(written);
    }
    return {};
  }

  [[nodiscard]] auto receive(std::span<std::byte> output, Clock::time_point deadline)
      -> liquid::result<void> {
    while (!output.empty()) {
      auto available = DWORD{};
      if (!PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr)) {
        return std::unexpected(process_error(liquid::errc::unavailable, exit_text()));
      }
      if (available == 0) {
        if (Clock::now() >= deadline) {
          return std::unexpected(
              process_error(liquid::errc::unavailable, "plugin host did not answer in time"));
        }
        WaitForSingleObject(process, 1);
        continue;
      }
      auto count = DWORD{};
      const auto wanted = static_cast<DWORD>(std::min<std::size_t>(output.size(), available));
      if (!ReadFile(read, output.data(), wanted, &count, nullptr) || count == 0) {
        return std::unexpected(process_error(liquid::errc::unavailable, exit_text()));
      }
      output = output.subspan(count);
    }
    return {};
  }
};

namespace {

auto quoted(std::wstring_view value) -> std::wstring {
  auto output = std::wstring(L"\"");
  auto slashes = std::size_t{};
  for (const auto character : value) {
    if (character == L'\\') {
      ++slashes;
      continue;
    }
    if (character == L'"') {
      output.append(slashes * 2 + 1, L'\\');
    } else {
      output.append(slashes, L'\\');
    }
    slashes = 0;
    output.push_back(character);
  }
  output.append(slashes * 2, L'\\');
  output.push_back(L'"');
  return output;
}

auto last_error(std::string action) -> liquid::error {
  return process_error(liquid::errc::unavailable, std::move(action),
                       "error " + std::to_string(GetLastError()));
}

// A primary token like ours at low integrity: the host can read but not write the user's files.
auto low_integrity_token() -> liquid::result<HANDLE> {
  auto current = HANDLE{};
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY,
                        &current)) {
    return std::unexpected(last_error("could not open the process token"));
  }
  auto primary = HANDLE{};
  const auto duplicated = DuplicateTokenEx(current, 0, nullptr, SecurityImpersonation,
                                           TokenPrimary, &primary);
  CloseHandle(current);
  if (!duplicated) {
    return std::unexpected(last_error("could not duplicate the process token"));
  }
  PSID low = nullptr;
  if (!ConvertStringSidToSidW(L"S-1-16-4096", &low)) {
    CloseHandle(primary);
    return std::unexpected(last_error("could not create the low integrity SID"));
  }
  auto label = TOKEN_MANDATORY_LABEL{};
  label.Label.Attributes = SE_GROUP_INTEGRITY;
  label.Label.Sid = low;
  const auto set = SetTokenInformation(primary, TokenIntegrityLevel, &label,
                                       sizeof(label) + GetLengthSid(low));
  LocalFree(low);
  if (!set) {
    CloseHandle(primary);
    return std::unexpected(last_error("could not lower the plugin host's integrity"));
  }
  return primary;
}

auto confining_job() -> liquid::result<HANDLE> {
  auto *job = CreateJobObjectW(nullptr, nullptr);
  if (job == nullptr) {
    return std::unexpected(last_error("could not create the plugin job"));
  }
  auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                            JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                                            JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
  limits.BasicLimitInformation.ActiveProcessLimit = 1;
  auto interface_limits = JOBOBJECT_BASIC_UI_RESTRICTIONS{};
  interface_limits.UIRestrictionsClass =
      JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
      JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
      JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
      JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
      !SetInformationJobObject(job, JobObjectBasicUIRestrictions, &interface_limits,
                               sizeof(interface_limits))) {
    CloseHandle(job);
    return std::unexpected(last_error("could not limit the plugin job"));
  }
  return job;
}

auto spawn(const std::filesystem::path &host, const std::filesystem::path &plugin)
    -> liquid::result<std::unique_ptr<PluginProcess::Child>> {
  auto child = std::make_unique<PluginProcess::Child>();
  auto inheritable = SECURITY_ATTRIBUTES{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE host_read = nullptr;
  HANDLE host_write = nullptr;
  if (!CreatePipe(&host_read, &child->write, &inheritable, 1U << 20U)) {
    return std::unexpected(last_error("could not create the plugin channel"));
  }
  if (!CreatePipe(&child->read, &host_write, &inheritable, 1U << 20U)) {
    CloseHandle(host_read);
    return std::unexpected(last_error("could not create the plugin channel"));
  }
  const auto close_host_ends = [&] {
    CloseHandle(host_read);
    CloseHandle(host_write);
  };
  SetHandleInformation(child->write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(child->read, HANDLE_FLAG_INHERIT, 0);

  auto token = low_integrity_token();
  if (!token) {
    close_host_ends();
    return std::unexpected(token.error());
  }
  auto job = confining_job();
  if (!job) {
    CloseHandle(*token);
    close_host_ends();
    return std::unexpected(job.error());
  }
  child->job = *job;

  // Only the two channel handles are inherited.
  auto attribute_size = SIZE_T{};
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
  auto attribute_storage = std::vector<std::byte>(attribute_size);
  auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
  auto handles = std::array<HANDLE, 2>{host_read, host_write};
  if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size) ||
      !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                                 sizeof(handles), nullptr, nullptr)) {
    CloseHandle(*token);
    close_host_ends();
    return std::unexpected(last_error("could not limit the plugin host's handles"));
  }
  auto startup = STARTUPINFOEXW{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList = attributes;

  auto command = quoted(host.wstring()) + L" --plugin " + quoted(plugin.wstring()) +
                 L" --channel " +
                 std::to_wstring(reinterpret_cast<std::uintptr_t>(host_read)) + L" " +
                 std::to_wstring(reinterpret_cast<std::uintptr_t>(host_write));
  // A scrubbed environment: no API keys or tokens reach the plugin.
  auto environment = std::wstring{};
  if (const auto *root = _wgetenv(L"SystemRoot")) {
    environment += L"SystemRoot=";
    environment += root;
    environment.push_back(L'\0');
  }
  environment.push_back(L'\0');
  environment.push_back(L'\0');

  auto information = PROCESS_INFORMATION{};
  const auto created = CreateProcessAsUserW(
      *token, nullptr, command.data(), nullptr, nullptr, TRUE,
      CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT |
          CREATE_NO_WINDOW,
      environment.data(), nullptr, &startup.StartupInfo, &information);
  const auto created_error = GetLastError();
  DeleteProcThreadAttributeList(attributes);
  CloseHandle(*token);
  close_host_ends();
  if (!created) {
    SetLastError(created_error);
    return std::unexpected(last_error("could not start the plugin host"));
  }
  child->process = information.hProcess;
  if (!AssignProcessToJobObject(child->job, information.hProcess)) {
    TerminateProcess(information.hProcess, 1);
    CloseHandle(information.hThread);
    return std::unexpected(last_error("could not confine the plugin host"));
  }
  ResumeThread(information.hThread);
  CloseHandle(information.hThread);
  return child;
}

} // namespace

#else // POSIX

auto plugin_host_executable() -> std::filesystem::path {
  constexpr auto name = "liquify-plugin-host";
  auto executable = std::filesystem::path{};
#if defined(__APPLE__)
  auto size = std::uint32_t{};
  _NSGetExecutablePath(nullptr, &size);
  auto buffer = std::string(size, '\0');
  if (size != 0 && _NSGetExecutablePath(buffer.data(), &size) == 0) {
    executable = std::filesystem::path(buffer.c_str());
  }
#else
  auto code = std::error_code{};
  executable = std::filesystem::read_symlink("/proc/self/exe", code);
#endif
  if (!executable.empty()) {
    auto code = std::error_code{};
    const auto resolved = std::filesystem::weakly_canonical(executable, code);
    const auto adjacent = (code ? executable : resolved).parent_path() / name;
    if (std::filesystem::exists(adjacent, code)) {
      return adjacent;
    }
  }
#if defined(LIQUIFY_PLUGIN_HOST)
  return std::filesystem::path(LIQUIFY_PLUGIN_HOST);
#else
  return name;
#endif
}

struct PluginProcess::Child {
  pid_t pid = -1;
  int channel = -1;
  std::optional<int> status;

  Child() = default;
  Child(const Child &) = delete;
  auto operator=(const Child &) -> Child & = delete;

  ~Child() {
    if (channel >= 0) {
      ::close(channel); // the host sees the end of its channel and exits
    }
    if (pid > 0 && !status) {
      for (int attempt = 0; attempt < 100 && !reap(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (!status) {
        kill();
      }
    }
  }

  [[nodiscard]] auto reap() -> bool {
    if (status) {
      return true;
    }
    auto value = 0;
    const auto result = waitpid(pid, &value, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD)) {
      status = value;
      return true;
    }
    return false;
  }

  void kill() {
    if (pid > 0 && !status) {
      ::kill(pid, SIGKILL);
      auto value = 0;
      while (waitpid(pid, &value, 0) < 0 && errno == EINTR) {
      }
      status = value;
    }
  }

  [[nodiscard]] auto exit_text() -> std::string {
    for (int attempt = 0; attempt < 20 && !reap(); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!status) {
      return "plugin host closed its channel";
    }
    if (WIFSIGNALED(*status)) {
      return "plugin host stopped on signal " + std::to_string(WTERMSIG(*status));
    }
    return "plugin host exited with status " + std::to_string(WEXITSTATUS(*status));
  }

  [[nodiscard]] auto wait(short events, Clock::time_point deadline) -> liquid::result<void> {
    for (;;) {
      auto descriptor = pollfd{channel, events, 0};
      const auto ready = ::poll(&descriptor, 1, static_cast<int>(remaining(deadline).count()));
      if (ready > 0) {
        return {};
      }
      if (ready < 0 && errno == EINTR) {
        continue;
      }
      if (ready == 0) {
        return std::unexpected(
            process_error(liquid::errc::unavailable, "plugin host did not answer in time"));
      }
      return std::unexpected(process_error(liquid::errc::unavailable, exit_text()));
    }
  }

  [[nodiscard]] auto send(std::span<const std::byte> bytes, Clock::time_point deadline)
      -> liquid::result<void> {
#if defined(MSG_NOSIGNAL)
    constexpr auto flags = MSG_NOSIGNAL;
#else
    constexpr auto flags = 0; // SO_NOSIGPIPE is set on the socket
#endif
    while (!bytes.empty()) {
      const auto written = ::send(channel, bytes.data(), bytes.size(), flags);
      if (written > 0) {
        bytes = bytes.subspan(static_cast<std::size_t>(written));
        continue;
      }
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (auto ready = wait(POLLOUT, deadline); !ready) {
          return ready;
        }
        continue;
      }
      return std::unexpected(process_error(liquid::errc::unavailable, exit_text()));
    }
    return {};
  }

  [[nodiscard]] auto receive(std::span<std::byte> output, Clock::time_point deadline)
      -> liquid::result<void> {
    while (!output.empty()) {
      const auto read = ::recv(channel, output.data(), output.size(), 0);
      if (read > 0) {
        output = output.subspan(static_cast<std::size_t>(read));
        continue;
      }
      if (read < 0 && errno == EINTR) {
        continue;
      }
      if (read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (auto ready = wait(POLLIN, deadline); !ready) {
          return ready;
        }
        continue;
      }
      return std::unexpected(process_error(liquid::errc::unavailable, exit_text()));
    }
    return {};
  }
};

namespace {

// Environment variables the host keeps; everything else (API keys, tokens, agent sockets) is
// dropped before the plugin runs.
constexpr auto kept_environment =
    std::array{"PATH", "LANG", "LC_ALL", "LC_CTYPE", "TZ", "LD_LIBRARY_PATH", "HOME"};

auto spawn(const std::filesystem::path &host, const std::filesystem::path &plugin)
    -> liquid::result<std::unique_ptr<PluginProcess::Child>> {
  auto sockets = std::array<int, 2>{-1, -1};
#if defined(SOCK_CLOEXEC)
  const auto created = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets.data());
#else
  const auto created = ::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data());
#endif
  if (created != 0) {
    return std::unexpected(process_error(liquid::errc::unavailable,
                                         "could not create the plugin channel",
                                         std::strerror(errno)));
  }
  for (const auto descriptor : sockets) {
    ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
  }
#if defined(SO_NOSIGPIPE)
  auto enabled = 1;
  ::setsockopt(sockets[0], SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif

  // Everything the child needs is prepared before fork: after it only async-signal-safe calls.
  auto host_text = host.string();
  auto plugin_text = plugin.string();
  auto plugin_flag = std::string("--plugin");
  auto arguments = std::array<char *, 4>{host_text.data(), plugin_flag.data(), plugin_text.data(),
                                         nullptr};
  auto environment_text = std::vector<std::string>{};
  for (const auto *key : kept_environment) {
    if (const auto *value = std::getenv(key)) {
      environment_text.push_back(std::string(key) + "=" + value);
    }
  }
  auto environment = std::vector<char *>{};
  for (auto &entry : environment_text) {
    environment.push_back(entry.data());
  }
  environment.push_back(nullptr);
  const auto open_max = ::sysconf(_SC_OPEN_MAX);
  const auto last_descriptor =
      static_cast<int>(std::clamp<long>(open_max < 0 ? 1024 : open_max, 256, 65536));

  const auto pid = ::fork();
  if (pid < 0) {
    ::close(sockets[0]);
    ::close(sockets[1]);
    return std::unexpected(process_error(liquid::errc::unavailable,
                                         "could not start the plugin host", std::strerror(errno)));
  }
  if (pid == 0) {
    if (sockets[1] == 3) {
      ::fcntl(3, F_SETFD, 0);
    } else if (::dup2(sockets[1], 3) < 0) {
      _exit(127);
    }
    const auto null = ::open("/dev/null", O_RDWR);
    if (null >= 0) {
      ::dup2(null, 0);
      ::dup2(null, 1);
    }
    for (int descriptor = 4; descriptor < last_descriptor; ++descriptor) {
      ::close(descriptor);
    }
#if defined(__linux__)
    ::prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
    ::execve(arguments[0], arguments.data(), environment.data());
    _exit(127);
  }
  ::close(sockets[1]);
  const auto flags = ::fcntl(sockets[0], F_GETFL);
  ::fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK);
  auto child = std::make_unique<PluginProcess::Child>();
  child->pid = pid;
  child->channel = sockets[0];
  return child;
}

} // namespace

#endif

namespace {

auto send_frame(PluginProcess::Child &child, std::span<const std::byte> frame,
                Clock::time_point deadline) -> liquid::result<void> {
  if (frame.size() > protocol::max_frame_bytes) {
    return std::unexpected(
        process_error(liquid::errc::invalid_argument, "plugin value is too large"));
  }
  const auto header = protocol::frame_header(frame.size());
  if (auto sent = child.send(header, deadline); !sent) {
    return sent;
  }
  return child.send(frame, deadline);
}

auto receive_frame(PluginProcess::Child &child, Clock::time_point deadline)
    -> liquid::result<std::vector<std::byte>> {
  auto header = std::array<std::byte, 4>{};
  if (auto received = child.receive(header, deadline); !received) {
    return std::unexpected(received.error());
  }
  const auto size = protocol::frame_size(header);
  if (size > protocol::max_frame_bytes) {
    return std::unexpected(
        process_error(liquid::errc::corrupt, "plugin host sent an oversized frame"));
  }
  auto frame = std::vector<std::byte>(size);
  if (auto received = child.receive(frame, deadline); !received) {
    return std::unexpected(received.error());
  }
  return frame;
}

auto start_child(const std::filesystem::path &host, const std::filesystem::path &plugin)
    -> liquid::result<std::pair<std::unique_ptr<PluginProcess::Child>, protocol::Manifest>> {
  auto code = std::error_code{};
  if (!std::filesystem::exists(host, code)) {
    return std::unexpected(
        process_error(liquid::errc::not_found, "plugin host is missing", host.string()));
  }
  auto child = spawn(host, plugin);
  if (!child) {
    return std::unexpected(child.error());
  }
  auto frame = receive_frame(**child, Clock::now() + PluginProcess::start_deadline);
  if (!frame) {
    (*child)->kill();
    return std::unexpected(process_error(frame.error().code, frame.error().message,
                                         plugin.string()));
  }
  auto manifest = protocol::decode_manifest(*frame);
  if (!manifest) {
    (*child)->kill();
    return std::unexpected(process_error(liquid::errc::corrupt, manifest.error(), plugin.string()));
  }
  if (!manifest->error.empty()) {
    (*child)->kill();
    return std::unexpected(
        process_error(liquid::errc::unavailable, manifest->error, plugin.string()));
  }
  return std::pair{std::move(*child), std::move(*manifest)};
}

} // namespace

PluginProcess::PluginProcess(std::filesystem::path host, std::filesystem::path plugin)
    : host_(std::move(host)), plugin_(std::move(plugin)) {}

PluginProcess::~PluginProcess() = default;

auto PluginProcess::start(std::filesystem::path host, std::filesystem::path plugin)
    -> liquid::result<std::shared_ptr<PluginProcess>> {
  auto started = start_child(host, plugin);
  if (!started) {
    return std::unexpected(started.error());
  }
  auto process =
      std::shared_ptr<PluginProcess>(new PluginProcess(std::move(host), std::move(plugin)));
  process->child_ = std::move(started->first);
  process->manifest_ = std::move(started->second);
  return process;
}

void PluginProcess::stop() {
  if (child_) {
    child_->kill();
    child_.reset();
  }
}

auto PluginProcess::exchange(const protocol::Request &request, std::chrono::milliseconds deadline)
    -> liquid::result<protocol::Response> {
  if (!child_) {
    auto started = start_child(host_, plugin_);
    if (!started) {
      return std::unexpected(started.error());
    }
    if (started->second != manifest_) {
      started->first->kill();
      return std::unexpected(process_error(liquid::errc::corrupt,
                                           "plugin changed its registrations on restart",
                                           plugin_.string()));
    }
    child_ = std::move(started->first);
  }
  const auto until = Clock::now() + deadline;
  if (auto sent = send_frame(*child_, protocol::encode(request), until); !sent) {
    return std::unexpected(sent.error());
  }
  auto frame = receive_frame(*child_, until);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  auto response = protocol::decode_response(*frame);
  if (!response) {
    return std::unexpected(process_error(liquid::errc::corrupt, response.error()));
  }
  return std::move(*response);
}

auto PluginProcess::call(protocol::Kind kind, std::string_view name,
                         std::span<const std::byte> input, std::chrono::milliseconds deadline)
    -> liquid::result<std::vector<std::byte>> {
  const auto lock = std::scoped_lock(mutex_);
  if (!child_ && failures_ > max_restarts) {
    return std::unexpected(last_error_);
  }
  const auto context = manifest_.name + ":" + std::string(name);
  auto response =
      exchange({kind, std::string(name), std::vector<std::byte>(input.begin(), input.end())},
               deadline);
  if (!response) {
    // The host is in an unknown state (hung, exited or out of step): end it; the next call
    // starts a fresh one until the restart budget is spent.
    stop();
    ++failures_;
    last_error_ = process_error(response.error().code, response.error().message, context);
    if (failures_ > max_restarts) {
      last_error_.message += " (plugin disabled after " + std::to_string(max_restarts) +
                             " restarts)";
    }
    return std::unexpected(last_error_);
  }
  if (response->status != LIQUID_FRONTEND_STATUS_OK) {
    return std::unexpected(process_error(status_code(response->status),
                                         response->message.empty() ? "plugin operation failed"
                                                                   : response->message,
                                         context));
  }
  return std::move(response->result);
}

} // namespace liquify::detail
