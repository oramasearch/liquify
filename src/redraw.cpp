#include "liquify/redraw.hpp"

#include <algorithm>
#include <chrono>

namespace liquify {

auto idle_wait(const RedrawInputs &inputs) -> std::chrono::milliseconds {
  // Something already wants this frame.
  if (inputs.repaint_requested || inputs.interacting) {
    return std::chrono::milliseconds::zero();
  }

  auto budget = max_idle_wait;
  if (inputs.async_in_flight) {
    budget = std::min(budget, async_poll_interval);
  }

  // A scheduled refresh that falls inside the budget shortens it; one already due
  // means draw now. duration::max() (nothing scheduled) must not be converted, so
  // it is compared in its own units first.
  if (inputs.until_scheduled < std::chrono::steady_clock::duration::max()) {
    if (inputs.until_scheduled <= std::chrono::steady_clock::duration::zero()) {
      return std::chrono::milliseconds::zero();
    }
    const auto scheduled =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            inputs.until_scheduled);
    budget = std::min(budget, scheduled);
  }

  return std::max(budget, std::chrono::milliseconds::zero());
}

} // namespace liquify
