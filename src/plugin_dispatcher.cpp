// SPDX-License-Identifier: MIT

// Plugin requests off the UI thread (liquify#149). Each plugin gets one worker thread, which
// runs that plugin's panel refreshes, commands and events one at a time, in the order they were
// sent. The window reads the answers recorded so far and never waits for one.
//
// The workers are detached and own the state (and the host) through a shared pointer, so destroying the
// dispatcher never waits for a plugin: a request still running finishes on its worker (bounded
// by the host's deadline), its answer is dropped, and the worker exits.

#include "liquify/plugin.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace liquify {

namespace {

auto error_text(const liquid::error &error) -> std::string {
  auto text = error.message;
  if (!error.context.empty()) {
    text += ": " + error.context;
  }
  return text;
}

} // namespace

struct PluginDispatcher::State {
  struct Lane {
    std::string plugin;
    std::deque<std::function<void(State &)>> jobs;
    bool panel_refresh_pending = false; // queued or running
    bool running = false;
  };

  std::shared_ptr<PluginHost> host;
  std::function<void()> on_answer;
  mutable std::mutex mutex;
  std::condition_variable wake;
  bool stopping = false;
  std::vector<Lane> lanes;
  std::vector<std::pair<std::string, std::string>> command_plugins; // command -> plugin
  std::vector<PluginPanelSnapshot> panels;
  std::vector<PluginCommandOutcome> outcomes;
  std::vector<std::string> running_commands;
  std::uint64_t generation = 0;

  // Called with `mutex` held, after an answer is recorded.
  void answered() {
    ++generation;
    if (on_answer) {
      on_answer();
    }
  }

  auto lane(std::string_view plugin) -> Lane * {
    const auto found = std::ranges::find(lanes, plugin, &Lane::plugin);
    return found == lanes.end() ? nullptr : &*found;
  }

  void enqueue(Lane &lane, std::function<void(State &)> job) {
    lane.jobs.push_back(std::move(job));
    wake.notify_all();
  }

  static void work(const std::shared_ptr<State> &state, std::size_t index) {
    auto lock = std::unique_lock(state->mutex);
    for (;;) {
      state->wake.wait(lock, [&] {
        return state->stopping || !state->lanes[index].jobs.empty();
      });
      if (state->stopping) {
        return;
      }
      auto job = std::move(state->lanes[index].jobs.front());
      state->lanes[index].jobs.pop_front();
      state->lanes[index].running = true;
      lock.unlock();
      job(*state);
      lock.lock();
      state->lanes[index].running = false;
    }
  }
};

PluginDispatcher::PluginDispatcher(std::shared_ptr<PluginHost> host,
                                   std::function<void()> on_answer)
    : state_(std::make_shared<State>()) {
  state_->host = std::move(host);
  state_->on_answer = std::move(on_answer);
  if (!state_->host) {
    return;
  }
  state_->panels = state_->host->panel_list();
  for (const auto &command : state_->host->commands()) {
    state_->command_plugins.emplace_back(command.name, command.plugin);
  }
  for (auto &name : state_->host->plugin_names()) {
    if (!state_->lane(name)) {
      state_->lanes.push_back({std::move(name), {}, false, false});
    }
  }
  for (std::size_t index = 0; index < state_->lanes.size(); ++index) {
    std::thread([state = state_, index] { State::work(state, index); }).detach();
  }
}

PluginDispatcher::~PluginDispatcher() {
  const auto lock = std::scoped_lock(state_->mutex);
  state_->stopping = true;
  state_->on_answer = nullptr;
  for (auto &lane : state_->lanes) {
    lane.jobs.clear();
  }
  state_->wake.notify_all();
}

void PluginDispatcher::refresh_panels(std::span<const std::byte> context_cbor) {
  const auto lock = std::scoped_lock(state_->mutex);
  for (auto &lane : state_->lanes) {
    const auto has_panels = std::ranges::any_of(
        state_->panels, [&](const auto &panel) { return panel.plugin == lane.plugin; });
    if (!has_panels || lane.panel_refresh_pending) {
      continue;
    }
    lane.panel_refresh_pending = true;
    state_->enqueue(
        lane, [plugin = lane.plugin,
               context = std::vector<std::byte>(context_cbor.begin(), context_cbor.end())](
                  State &state) {
          auto answers = state.host->panels(plugin, context);
          const auto lock = std::scoped_lock(state.mutex);
          if (auto *lane = state.lane(plugin)) {
            lane->panel_refresh_pending = false;
          }
          if (state.stopping) {
            return;
          }
          for (auto &answer : answers) {
            const auto found = std::ranges::find_if(state.panels, [&](const auto &panel) {
              return panel.plugin == answer.plugin && panel.name == answer.name;
            });
            if (found != state.panels.end()) {
              *found = std::move(answer);
            }
          }
          state.answered();
        });
  }
}

auto PluginDispatcher::invoke_command(std::string_view name,
                                      std::span<const std::byte> arguments_cbor)
    -> liquid::result<void> {
  const auto lock = std::scoped_lock(state_->mutex);
  const auto command = std::ranges::find(state_->command_plugins, name,
                                         &std::pair<std::string, std::string>::first);
  auto *lane = command == state_->command_plugins.end() ? nullptr
                                                        : state_->lane(command->second);
  if (!lane) {
    return std::unexpected(liquid::error{.code = liquid::errc::not_found,
                                         .message = "plugin command is not registered",
                                         .context = std::string(name)});
  }
  state_->running_commands.emplace_back(name);
  state_->enqueue(
      *lane, [name = std::string(name),
              arguments = std::vector<std::byte>(arguments_cbor.begin(), arguments_cbor.end())](
                 State &state) {
        auto result = state.host->invoke_command(name, arguments);
        const auto lock = std::scoped_lock(state.mutex);
        const auto running = std::ranges::find(state.running_commands, name);
        if (running != state.running_commands.end()) {
          state.running_commands.erase(running);
        }
        if (state.stopping) {
          return;
        }
        auto outcome = PluginCommandOutcome{.name = name};
        if (result) {
          outcome.result_cbor = std::move(*result);
        } else {
          outcome.error = error_text(result.error());
        }
        state.outcomes.push_back(std::move(outcome));
        state.answered();
      });
  return {};
}

void PluginDispatcher::publish_presence(const liquid::presence &value) {
  const auto lock = std::scoped_lock(state_->mutex);
  for (auto &lane : state_->lanes) {
    state_->enqueue(lane, [plugin = lane.plugin, value](State &state) {
      state.host->publish_presence(value, plugin);
    });
  }
}

void PluginDispatcher::publish_lease(const liquid::semantic_lease &value, bool active) {
  const auto lock = std::scoped_lock(state_->mutex);
  for (auto &lane : state_->lanes) {
    state_->enqueue(lane, [plugin = lane.plugin, value, active](State &state) {
      state.host->publish_lease(value, active, plugin);
    });
  }
}

void PluginDispatcher::publish_attribution(std::string_view path,
                                           const liquid::change_id &change,
                                           std::span<const liquid::line_attribution> lines) {
  const auto lock = std::scoped_lock(state_->mutex);
  for (auto &lane : state_->lanes) {
    state_->enqueue(lane, [plugin = lane.plugin, path = std::string(path), change,
                           lines = std::vector(lines.begin(), lines.end())](State &state) {
      state.host->publish_attribution(path, change, lines, plugin);
    });
  }
}

auto PluginDispatcher::panels() const -> std::vector<PluginPanelSnapshot> {
  const auto lock = std::scoped_lock(state_->mutex);
  return state_->panels;
}

auto PluginDispatcher::take_command_outcomes() -> std::vector<PluginCommandOutcome> {
  const auto lock = std::scoped_lock(state_->mutex);
  return std::exchange(state_->outcomes, {});
}

auto PluginDispatcher::running_commands() const -> std::vector<std::string> {
  const auto lock = std::scoped_lock(state_->mutex);
  return state_->running_commands;
}

auto PluginDispatcher::pending() const -> std::size_t {
  const auto lock = std::scoped_lock(state_->mutex);
  auto count = std::size_t{};
  for (const auto &lane : state_->lanes) {
    count += lane.jobs.size() + (lane.running ? 1U : 0U);
  }
  return count;
}

auto PluginDispatcher::generation() const -> std::uint64_t {
  const auto lock = std::scoped_lock(state_->mutex);
  return state_->generation;
}

} // namespace liquify
