# The redraw model

liquify waits for a cause and then draws. It does not redraw on a schedule.

An immediate-mode interface rebuilds itself every frame — that is the library's model, not a
defect. Doing it when nothing has changed is, and it is what made an idle window cost roughly
three quarters of a core: `App::run()`
called `SDL_PollEvent`, which returns immediately, so the loop rebuilt and re-rendered the whole
interface for as long as the application was open. `SDL_SetRenderVSync` capped the *rate* at the
display refresh but did nothing about the *work*.

## What the loop does

Each iteration asks [`idle_wait`](../../include/liquify/redraw.hpp) how long it may block, waits
that long on `SDL_WaitEventTimeout`, drains the event queue, and draws one frame.

**Every wake produces a frame.** That is the property the design rests on: sleeping cannot swallow
a state change, because the loop never decides *not* to draw — it only decides how long it is safe
to wait first. A producer that changes UI state without saying so is late, never lost.

`idle_wait` returns zero — draw now, at the display rate — when:

- **a repaint was requested**, by an arriving SDL event or by `App::request_repaint()`;
- **the interface is busy**: an item is active, a mouse button is down, or the pointer or wheel
  moved since the last frame;
- **a scheduled refresh is already due**.

Otherwise it returns the time until the nearest scheduled refresh, capped at `max_idle_wait`
(200 ms). While a `std::async` result is outstanding it polls at `async_poll_interval` (16 ms), so
a completing future is noticed as promptly as it was before.

### What is deliberately not a busy signal

`ImGuiIO::WantCaptureMouse`, `WantCaptureKeyboard` and `WantTextInput` say where input *would be
routed*, not that anything is happening. They are true whenever the pointer is over the interface
or a text field has focus — which, in an editor, is nearly always. Treating them as activity keeps
the loop at the display rate and reproduces the original defect. They are not consulted.

A focused caret still blinks: the 200 ms cap wakes the loop several times a second, which is enough
for the blink and costs almost nothing.

## Adding an async producer

If your work can change a pixel, call `App::request_repaint()` when it completes. It is safe from
any thread — it sets a flag and pushes an SDL user event, which wakes a blocked
`SDL_WaitEventTimeout`.

If your work is polled from the frame path on a deadline, give it a
`std::chrono::steady_clock::time_point` member and add it to
`App::until_next_scheduled_refresh()`, so the loop sleeps until it is due instead of past it.

If your work is a `std::future` polled from `App::frame`, add it to `App::async_work_in_flight()`
so the loop keeps frame cadence while it is outstanding.

**A missed wake-up would be a hung window, which is worse than the busy loop this replaced.** Three
things prevent it: every wake draws; `max_idle_wait` bounds how long any change can go unseen; and
the scheduled deadlines are derived from the same members the polls use, so they cannot drift apart.

## Modes that still draw continuously

`--smoke` and `--screenshot` (anything with `max_frames > 0`) render a fixed number of frames as
fast as they can. They are measurements, not a window someone is sitting in front of, so they keep
the unconditional loop and their reported timings stay comparable across this change.

## Measured

On an idle window with a repository open, foreground, measured as CPU-seconds consumed over 30
seconds of wall time:

| | CPU used in 30 s | Share of a core |
|---|---|---|
| Before | 2.85 s | 9.5 % |
| After | 0.63 s | **2.1 %** |

That is this machine, on mains, with a small repository — the per-frame cost scales with what is on
screen, so a large file open costs more per frame in both columns. The ratio is the point: the loop
draws a handful of frames a second when idle instead of sixty.

`--smoke 200` after the change reports `avg_frame_ms=14.703 p95_frame_ms=16.809
avg_typing_ms=16.329`, on the continuous path, unchanged by the idle policy.

## Tests

[`tests/test_redraw.cpp`](../../tests/test_redraw.cpp) covers the policy directly, including the
property the issue is about — with nothing happening the loop waits rather than drawing — and that
the wait is always bounded.
