// SPDX-License-Identifier: MIT

#ifndef LIQUIFY_REDRAW_HPP
#define LIQUIFY_REDRAW_HPP

#include <chrono>

namespace liquify {

// How long the run loop may block before it must produce another frame.
//
// An immediate-mode interface rebuilds itself every frame; that is the library's
// model, not a defect. Doing it when nothing has changed is, which is what made
// an idle window cost roughly three quarters of a core (liquify #66).
//
// The loop therefore waits for a cause instead of spinning. Every wake produces a
// frame, so no state change can be missed by the decision to sleep — the only
// question this policy answers is how long it is safe to wait.

// The longest the loop will ever block. It is a safety net, not the mechanism: a
// producer that changes UI state without asking for a repaint costs a bounded
// delay rather than a window that never updates. A missed wake-up is a hung UI,
// which is worse than the busy loop this replaces.
inline constexpr std::chrono::milliseconds max_idle_wait{200};

// While a future is outstanding the loop polls at roughly frame cadence, so a
// result is picked up as promptly as it was before.
inline constexpr std::chrono::milliseconds async_poll_interval{16};

// What the loop knows when it decides whether to sleep.
struct RedrawInputs {
  // An event arrived, or a producer asked for a frame.
  bool repaint_requested = false;
  // The interface is mid-interaction or mid-animation: keep drawing at the
  // display rate.
  bool interacting = false;
  // A std::async result is outstanding and is polled from the frame path.
  bool async_in_flight = false;
  // Time until the nearest scheduled refresh (status, review, plugins,
  // multiplayer). Negative or zero means one is already due.
  std::chrono::steady_clock::duration until_scheduled =
      std::chrono::steady_clock::duration::max();
};

// Returns the SDL_WaitEventTimeout budget for one iteration. Zero means draw now.
[[nodiscard]] auto idle_wait(const RedrawInputs &inputs)
    -> std::chrono::milliseconds;

} // namespace liquify

#endif
