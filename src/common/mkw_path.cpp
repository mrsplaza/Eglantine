// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdlib>
#include <string>

#include "common/game_presets.h"
#include "common/logging.h"
#include "common/mkw_path.h"
#include "common/settings.h"

namespace Mkw {
namespace {

struct ComponentInfo {
    const char* name; ///< Suffix of its EDEN_MKW_<NAME> switch.
    bool default_on;  ///< On by default while Mario Kart World runs.
};

// Defaults follow the measurements: a component is switched on here only once it has been shown
// to make frame pacing more regular without side effects.
constexpr std::array<ComponentInfo, static_cast<size_t>(Component::Count)> Components{{
    {"IMAGE_RESERVE", true},
    {"GAME_TIME_PACING", true},
    {"DISPLAY_PACING", true},
}};

// Texture reserve used when ImageReserve is on and no size is given.
constexpr u64 DefaultImageReserveMiB = 2048;

int EnvSwitch(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    if (value == nullptr || value[0] == '\0') {
        return -1;
    }
    return value[0] == '0' ? 0 : 1;
}

} // Anonymous namespace

bool Active() {
    if (Settings::GetCurrentProgramID() != Settings::MarioKartWorldProgramId) {
        return false;
    }
    static const bool enabled = EnvSwitch("EDEN_MKW") != 0;
    return enabled;
}

bool Enabled(Component component) {
    if (!Active()) {
        return false;
    }
    const auto& info = Components[static_cast<size_t>(component)];
    const int forced = EnvSwitch(std::string{"EDEN_MKW_"} + info.name);
    return forced == -1 ? info.default_on : forced == 1;
}

u64 ImageReserveMiB() {
    if (!Enabled(Component::ImageReserve)) {
        return 0;
    }
    if (const char* value = std::getenv("EDEN_MKW_IMAGE_RESERVE_MIB")) {
        return std::strtoull(value, nullptr, 10);
    }
    return DefaultImageReserveMiB;
}

void LogSummary() {
    if (!Active()) {
        return;
    }
    std::string on;
    for (size_t i = 0; i < Components.size(); ++i) {
        if (Enabled(static_cast<Component>(i))) {
            on += on.empty() ? "" : ", ";
            on += Components[i].name;
        }
    }
    LOG_INFO(Core, "Mario Kart World dedicated path: on ({})", on.empty() ? "no components" : on);
}

} // namespace Mkw
