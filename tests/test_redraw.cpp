// SPDX-License-Identifier: MIT

#include "liquify/redraw.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

using namespace std::chrono_literals;

// Nothing is happening: the loop sleeps rather than redrawing at the display
// rate. This is the property liquify #66 is about — an idle window must stop
// producing frames, not produce them more cheaply.
TEST(Redraw, IdleLoopWaitsInsteadOfDrawing) {
  const auto wait = liquify::idle_wait({});
  EXPECT_GT(wait, 0ms);
  EXPECT_EQ(wait, liquify::max_idle_wait);
}

TEST(Redraw, RequestedRepaintDrawsImmediately) {
  EXPECT_EQ(liquify::idle_wait({.repaint_requested = true}), 0ms);
}

// Typing, scrolling and animation must not pay for the idle policy.
TEST(Redraw, InteractionDrawsImmediately) {
  EXPECT_EQ(liquify::idle_wait({.interacting = true}), 0ms);
}

// A future is polled from the frame path, so while one is outstanding the loop
// keeps frame cadence and picks the result up as promptly as it did before.
TEST(Redraw, OutstandingAsyncWorkPollsAtFrameCadence) {
  const auto wait = liquify::idle_wait({.async_in_flight = true});
  EXPECT_EQ(wait, liquify::async_poll_interval);
  EXPECT_LT(wait, liquify::max_idle_wait);
}

TEST(Redraw, ScheduledRefreshInsideTheBudgetShortensTheWait) {
  const auto wait = liquify::idle_wait({.until_scheduled = 50ms});
  EXPECT_EQ(wait, 50ms);
}

TEST(Redraw, ScheduledRefreshBeyondTheBudgetDoesNotExtendIt) {
  const auto wait = liquify::idle_wait({.until_scheduled = 2s});
  EXPECT_EQ(wait, liquify::max_idle_wait);
}

TEST(Redraw, RefreshAlreadyDueDrawsImmediately) {
  EXPECT_EQ(liquify::idle_wait({.until_scheduled = 0ms}), 0ms);
  EXPECT_EQ(liquify::idle_wait({.until_scheduled = -5ms}), 0ms);
}

// The cap is what keeps a forgotten repaint request from becoming a hung window:
// the loop always wakes, polls and draws within a bounded time no matter what the
// producers do.
TEST(Redraw, WaitIsAlwaysBounded) {
  EXPECT_LE(liquify::idle_wait({}), liquify::max_idle_wait);
  EXPECT_LE(liquify::idle_wait({.until_scheduled =
                                    std::chrono::steady_clock::duration::max()}),
            liquify::max_idle_wait);
  EXPECT_LE(liquify::idle_wait({.async_in_flight = true,
                                .until_scheduled = 10s}),
            liquify::max_idle_wait);
}

// The shortest applicable deadline wins when several apply at once.
TEST(Redraw, NearestDeadlineWins) {
  EXPECT_EQ(liquify::idle_wait({.async_in_flight = true, .until_scheduled = 5ms}),
            5ms);
  EXPECT_EQ(
      liquify::idle_wait({.async_in_flight = true, .until_scheduled = 500ms}),
      liquify::async_poll_interval);
}

} // namespace
