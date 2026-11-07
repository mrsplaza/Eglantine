// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace Vulkan {

/// Presents a fixed frame rate on the display's own refresh grid.
///
/// A frame limiter driven only by the CPU clock queues frames at an arbitrary phase relative to
/// the monitor's vertical blank. When that phase sits close to a refresh boundary, scheduling
/// jitter of a millisecond or two moves frames across it and the screen alternates between
/// holding a frame for one and three refreshes (a visible stutter at a steady frame rate). This
/// pacer reads the refresh period and the last vertical blank from the desktop compositor and
/// queues each frame half a refresh before the boundary it is meant for, so every frame stays on
/// screen for the same number of refreshes. It only changes when frames are handed to the
/// display, never how fast the game runs: if frames come in late it shows them at the next
/// refresh, if they queue up it catches up instead of slowing the emulation down.
class DisplayPacer {
public:
    /// Waits for the next slot of a `target_fps` cadence on the refresh grid.
    /// `backlog` tells that another frame is already waiting behind this one.
    /// Returns false (without waiting) when the display cannot show `target_fps` evenly, e.g. a
    /// 144 Hz refresh for 30 fps, or when the refresh timing is unavailable; the caller then keeps
    /// its own limiter.
    bool Wait(double target_fps, bool backlog);

private:
    bool ReadDisplayTiming();
    void Report(bool active, double target_fps);

    std::int64_t freq = 0;      ///< Performance counter ticks per second.
    std::int64_t period = 0;    ///< Refresh period, in ticks.
    std::int64_t vblank = 0;    ///< Last known vertical blank, in ticks.
    std::int64_t last_slot = 0; ///< When the previous frame was queued, in ticks.
    std::int64_t refreshes = 0; ///< Refreshes per frame of the current cadence.
    bool reported_active = false;
    bool reported_inactive = false;

    // Statistics, logged every 600 frames.
    std::uint32_t frames = 0;
    std::uint32_t late = 0;
    std::uint32_t caught_up = 0;
    std::int64_t max_wait = 0;
    std::int64_t max_stale = 0; ///< Age of the compositor's last vertical blank when read.
};

} // namespace Vulkan
