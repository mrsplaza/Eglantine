// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

#include "common/common_types.h"

/// Mario Kart World's dedicated path: components that only run while title 0400C3F00006E000 is
/// loaded, each with its own switch so they can be compared. Every other title keeps Eglantine's
/// standard behaviour. None of them changes the game's timing (its frames stay paced by the guest).
///
/// EDEN_MKW=0 turns the whole path off. EDEN_MKW_<COMPONENT>=0/1 overrides one component.
namespace Mkw {

enum class Component : u32 {
    ImageReserve,   ///< Device memory kept for textures (no driver allocations per scene).
    GameTimePacing, ///< Dynamic mode only: frames shown for their game-time step.
    DisplayPacing,  ///< Fixed frame rate presented on the display's refresh grid (no stutter from
                    ///< a limiter that drifts against the vertical blank).
    Count,
};

/// Whether Mario Kart World is the running program and the dedicated path is on.
[[nodiscard]] bool Active();

/// Whether a component is on: its default for Mario Kart World, or its EDEN_MKW_<NAME> override.
[[nodiscard]] bool Enabled(Component component);

/// Texture reserve in MiB when ImageReserve is on (EDEN_MKW_IMAGE_RESERVE_MIB overrides).
[[nodiscard]] u64 ImageReserveMiB();

/// Logs which components are on, once per boot of the title.
void LogSummary();

} // namespace Mkw
