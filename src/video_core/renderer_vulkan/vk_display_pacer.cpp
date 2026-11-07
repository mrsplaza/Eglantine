// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "common/logging.h"
#include "video_core/renderer_vulkan/vk_display_pacer.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#endif
#endif

namespace Vulkan {
namespace {

std::int64_t Now() {
#ifdef _WIN32
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return value.QuadPart;
#else
    return 0;
#endif
}

double ToMs(std::int64_t ticks, std::int64_t freq) {
    return freq > 0 ? static_cast<double>(ticks) * 1000.0 / static_cast<double>(freq) : 0.0;
}

} // Anonymous namespace

bool DisplayPacer::ReadDisplayTiming() {
#ifdef _WIN32
    if (freq == 0) {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        freq = value.QuadPart;
    }
    const std::int64_t now = Now();
    if (mode_hz == 0 || now - mode_read > 2 * freq) {
        // Refresh rate of the display mode (whole Hz: 119 for a 119.88 Hz mode).
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
            if (mode_hz != 0 && mode_hz != mode.dmDisplayFrequency) {
                period = 0; // the user changed the refresh rate: start over
            }
            mode_hz = mode.dmDisplayFrequency;
        }
        mode_read = now;
    }
    DWM_TIMING_INFO info{};
    info.cbSize = sizeof(info);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &info)) && info.qpcRefreshPeriod != 0) {
        const double hz = static_cast<double>(freq) / static_cast<double>(info.qpcRefreshPeriod);
        // Accept the compositor's precise period only when it matches the mode (allowing the
        // fractional part the mode's whole-Hz value drops).
        if (mode_hz == 0 || (hz > mode_hz - 0.1 && hz < mode_hz + 1.0)) {
            period = static_cast<std::int64_t>(info.qpcRefreshPeriod);
            if (info.qpcVBlank != 0 && now - static_cast<std::int64_t>(info.qpcVBlank) < 8 * period) {
                vblank = static_cast<std::int64_t>(info.qpcVBlank);
            }
        } else {
            ++rejected;
        }
    }
    if (period == 0 && mode_hz > 1) {
        period = freq / mode_hz;
    }
    if (period != 0 && vblank == 0) {
        vblank = now; // no vertical blank seen yet: any anchor, corrected as soon as one is
    }
    return period != 0;
#else
    return false;
#endif
}

void DisplayPacer::Report(bool active, double target_fps) {
    const double hz = period > 0 ? static_cast<double>(freq) / static_cast<double>(period) : 0.0;
    if (active && !reported_active) {
        reported_active = true;
        reported_inactive = false;
        LOG_INFO(Render_Vulkan,
                 "Display pacing: {:.3f} Hz refresh, each frame shown for {} refreshes ({} fps)",
                 hz, refreshes, target_fps);
    } else if (!active && !reported_inactive) {
        reported_inactive = true;
        reported_active = false;
        if (period > 0) {
            LOG_WARNING(Render_Vulkan,
                        "Display pacing off: a {:.3f} Hz refresh cannot show {} fps evenly "
                        "(use a refresh that is a multiple of it, or variable refresh)",
                        hz, target_fps);
        } else {
            LOG_WARNING(Render_Vulkan, "Display pacing off: refresh timing unavailable");
        }
    }
}

bool DisplayPacer::Wait(double target_fps, bool backlog) {
    if (target_fps <= 0.0 || !ReadDisplayTiming()) {
        Report(false, target_fps);
        last_slot = 0;
        return false;
    }
    const double frame_ticks = static_cast<double>(freq) / target_fps;
    const std::int64_t n = std::llround(frame_ticks / static_cast<double>(period));
    if (n < 1 || std::abs(static_cast<double>(n * period) - frame_ticks) > 0.01 * frame_ticks) {
        Report(false, target_fps);
        last_slot = 0;
        return false;
    }
    refreshes = n;
    Report(true, target_fps);

    // Slots sit half a refresh before the boundary a frame is meant for: far from both edges,
    // so a frame queued there is picked up by the same refresh even with a few ms of jitter.
    const std::int64_t half = period / 2;
    const std::int64_t now = Now();
    max_stale = (std::max)(max_stale, now - vblank);
    const auto grid_at_or_after = [&](std::int64_t t) {
        const double k = std::ceil(static_cast<double>(t - vblank - half) / static_cast<double>(period));
        return vblank + static_cast<std::int64_t>(k) * period + half;
    };
    std::int64_t slot;
    if (last_slot == 0 || now - last_slot > 4 * n * period) {
        // First frame, or a long pause (loading, menus): start a new cadence at the next slot.
        slot = grid_at_or_after(now);
    } else if (backlog) {
        // Another frame is already waiting: the screen is behind the game. Show this one at the
        // next slot so the queue drains instead of throttling the emulation.
        slot = grid_at_or_after(now);
        ++caught_up;
    } else {
        // Keep the cadence exactly: n refreshes after the previous frame. The phase follows the
        // refresh grid by at most a quarter millisecond per frame, so a wandering or stale vertical
        // blank reading can never make two consecutive frames visibly uneven.
        const std::int64_t wanted = last_slot + n * period;
        const double x = static_cast<double>(wanted - vblank - half) / static_cast<double>(period);
        const std::int64_t error =
            static_cast<std::int64_t>((x - std::round(x)) * static_cast<double>(period));
        const std::int64_t step = freq / 4000;
        const std::int64_t correction = std::clamp(error, -step, step);
        max_correction = (std::max)(max_correction, correction < 0 ? -correction : correction);
        slot = wanted - correction;
        if (slot < now) {
            // Drawn after its slot: the previous frame stays one refresh longer. Continue the
            // cadence from the slot this one gets.
            slot = grid_at_or_after(now);
            ++late;
        }
    }

    const std::int64_t wait = slot - now;
    max_wait = (std::max)(max_wait, wait);
    if (wait > 0) {
        const std::int64_t spin = freq / 1000; // the last millisecond is spun for accuracy
        if (wait > 2 * spin) {
            std::this_thread::sleep_for(std::chrono::microseconds(
                static_cast<std::int64_t>(ToMs(wait - spin, freq) * 1000.0)));
        }
        while (Now() < slot) {
            std::this_thread::yield();
        }
    }
    last_slot = slot;

    if (++frames == 600) {
        LOG_INFO(Render_Vulkan,
                 "Display pacing: last 600 frames: {} late (one refresh longer), {} caught up, "
                 "longest wait {:.1f} ms, vblank age up to {:.1f} ms, largest phase step {:.2f} ms, "
                 "{} refresh readings ignored",
                 late, caught_up, ToMs(max_wait, freq), ToMs(max_stale, freq),
                 ToMs(max_correction, freq), rejected);
        frames = 0;
        late = 0;
        caught_up = 0;
        max_wait = 0;
        max_stale = 0;
        max_correction = 0;
        rejected = 0;
    }
    return true;
}

} // namespace Vulkan
