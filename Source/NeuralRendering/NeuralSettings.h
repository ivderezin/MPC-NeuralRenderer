// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace neural {
struct Settings {
    bool enabled = false;
    int intensity = 50;
    bool operator==(const Settings&) const = default;
    static Settings Load();
    long Save() const;
};
}
