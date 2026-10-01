// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cam::v4l2 {

struct ControlInfo {
    enum class Type { Integer, Boolean, Menu, IntegerMenu, Button };

    uint32_t id = 0;
    std::string name;
    Type type = Type::Integer;
    int64_t minimum = 0;
    int64_t maximum = 0;
    int64_t step = 1;
    int64_t defaultValue = 0;
    int64_t value = 0;
    bool is64 = false;
    bool readOnly = false;
    bool inactive = false; // e.g. manual exposure while auto exposure is on
    std::vector<std::pair<int64_t, std::string>> menu;

    // The automatic-mode control governing this one, if any (0 if none).
    uint32_t autoControl = 0;
};

// Enumerates all supported, well-formed controls. Malformed controls reported
// by buggy firmware are skipped rather than trusted.
std::vector<ControlInfo> enumerateControls(int fd);

// Refreshes value and flags of the given controls in place.
void refreshControls(int fd, std::vector<ControlInfo> &controls);

bool setControl(int fd, uint32_t id, int64_t value, std::string &error);
bool getControl(int fd, uint32_t id, int64_t &value, bool is64 = false);

// Grouping helpers for the UI.
enum class ControlGroup { Camera, Color, Advanced };
ControlGroup controlGroup(uint32_t id);
// For a manual control, the id of its auto-mode switch (or 0).
uint32_t autoControlFor(uint32_t id);

} // namespace cam::v4l2
