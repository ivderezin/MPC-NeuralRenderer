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
        settings.intensity = static_cast<int>((std::min)(value, 200ul));
    size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"DlssUpscaling", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
        ERROR_SUCCESS)
        settings.upscaler = value ? processing::Upscaler::Dlss : processing::Upscaler::Off;
    size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"Upscaler", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
        value <= 3)
        settings.upscaler = static_cast<processing::Upscaler>(value);
    size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"FrameGenerator", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
            ERROR_SUCCESS &&
        value <= 2)
        settings.frameGenerator = static_cast<processing::FrameGenerator>(value);
    size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"FrameMultiplier", RRF_RT_REG_DWORD, nullptr, &value, &size) ==
        ERROR_SUCCESS)
        settings.frameMultiplier = std::clamp(static_cast<unsigned>(value), 1u, 4u);
    return settings;
}
long Settings::Save() const {
    HKEY key = nullptr;
    auto error = RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS)
        return error;
    DWORD intensityValue = static_cast<DWORD>(std::clamp(intensity, 0, 200));
    DWORD enabledValue = enabled ? 1 : 0;
    error = RegSetValueExW(key, L"Intensity", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&intensityValue),
                           sizeof(intensityValue));
    if (error == ERROR_SUCCESS)
        error = RegSetValueExW(key, L"Enabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&enabledValue),
                               sizeof(enabledValue));
    DWORD upscaleValue = static_cast<DWORD>(upscaler);
    DWORD generatorValue = static_cast<DWORD>(frameGenerator);
    DWORD multiplierValue = std::clamp(frameMultiplier, 1u, 4u);
    if (error == ERROR_SUCCESS)
        error = RegSetValueExW(key, L"Upscaler", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&upscaleValue),
                               sizeof(upscaleValue));
    if (error == ERROR_SUCCESS)
        error = RegSetValueExW(key, L"FrameMultiplier", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&multiplierValue),
                               sizeof(multiplierValue));
    if (error == ERROR_SUCCESS)
        error = RegSetValueExW(key, L"FrameGenerator", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&generatorValue),
                               sizeof(generatorValue));
    RegCloseKey(key);
    return error;
}
}
