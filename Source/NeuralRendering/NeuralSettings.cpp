// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <algorithm>
#include "NeuralSettings.h"

namespace neural {
constexpr auto Key = L"Software\\MPC Neural Renderer";
Settings Settings::Load() {
    Settings settings;
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"Enabled", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        settings.enabled = value != 0;
    size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"Intensity", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        settings.intensity = static_cast<int>((std::min)(value, 100ul));
    return settings;
}
long Settings::Save() const {
    HKEY key = nullptr;
    auto error = RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) return error;
    DWORD intensityValue = static_cast<DWORD>(std::clamp(intensity, 0, 100));
    DWORD enabledValue = enabled ? 1 : 0;
    error = RegSetValueExW(key, L"Intensity", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&intensityValue), sizeof(intensityValue));
    if (error == ERROR_SUCCESS)
        error = RegSetValueExW(key, L"Enabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&enabledValue), sizeof(enabledValue));
    RegCloseKey(key);
    return error;
}
}
