// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include "../Source/NeuralRendering/NeuralSettings.h"
#include <iostream>
#include <stdexcept>
#include <string>
struct RegistryScope {
    HKEY root = nullptr;
    std::wstring path = L"Software\\MPC-NeuralRenderer-Tests-" + std::to_wstring(GetCurrentProcessId());
    RegistryScope() {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, nullptr) !=
            ERROR_SUCCESS)
            throw std::runtime_error("Cannot create isolated test registry");
        if (RegOverridePredefKey(HKEY_CURRENT_USER, root) != ERROR_SUCCESS) {
            RegCloseKey(root);
            RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
            throw std::runtime_error("Cannot isolate test registry");
        }
    }
    ~RegistryScope() {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(root);
        RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }
    void Write(const wchar_t* name, DWORD value) {
        HKEY key = nullptr;
        auto result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\MPC Neural Renderer", 0, nullptr, 0, KEY_SET_VALUE,
                                      nullptr, &key, nullptr);
        if (result == ERROR_SUCCESS) {
            result = RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
            RegCloseKey(key);
        }
        if (result != ERROR_SUCCESS)
            throw std::runtime_error("Cannot write test setting");
    }
};
void Require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        RegistryScope registry;
        using processing::FrameGenerator;
        using processing::Upscaler;
        auto settings = neural::Settings::Load();
        Require(settings.upscaler == Upscaler::Dlss && settings.frameMultiplier == 1 && settings.presentThread && settings.motionMethod == neural::MotionMethod::EchoStorm,
                "Defaults changed");
        registry.Write(L"DlssUpscaling", 0);
        Require(neural::Settings::Load().upscaler == Upscaler::Off, "Legacy disabled SR must migrate");
        for (auto upscaler : {Upscaler::Off, Upscaler::Dlss, Upscaler::Fsr, Upscaler::Xess})
            for (auto generator : {FrameGenerator::Dlss, FrameGenerator::Fsr, FrameGenerator::Xess}) {
                settings.upscaler = upscaler;
                settings.frameGenerator = generator;
                settings.frameMultiplier = 4;
                settings.enabled = true;
                settings.motionMethod = generator == FrameGenerator::Fsr ? neural::MotionMethod::EchoStorm : neural::MotionMethod::Fast;
                settings.amdBridge = generator == FrameGenerator::Fsr;
                settings.intensity = 200;
                settings.presentThread = generator == FrameGenerator::Fsr;
                settings.nrStage = static_cast<neural::NrStage>(static_cast<unsigned>(upscaler) % 3);
                Require(settings.Save() == ERROR_SUCCESS, "Save failed");
                Require(neural::Settings::Load() == settings, "Independent provider settings did not survive reload");
                Require(settings.FsrNative() == (upscaler == Upscaler::Fsr && generator == FrameGenerator::Fsr),
                        "Native FSR must require both the bridge option and FSR provider");
            }
        Require(settings.FsrNative() == (settings.upscaler == Upscaler::Fsr && settings.amdBridge), "Bridge must require FSR");
        registry.Write(L"MotionMethod", 99);
        Require(neural::Settings::Load().motionMethod == neural::MotionMethod::EchoStorm, "Unknown motion method must fall back to EchoStorm");
        registry.Write(L"Upscaler", 99);
        registry.Write(L"FrameGenerator", 99);
        registry.Write(L"FrameMultiplier", 99);
        registry.Write(L"NrStage", 99);
        settings = neural::Settings::Load();
        Require(settings.upscaler == Upscaler::Off && settings.frameGenerator == FrameGenerator::Dlss,
                "Invalid providers must fall back to valid settings");
        Require(settings.frameMultiplier == 4, "Invalid multiplier must clamp");
        Require(settings.nrStage == neural::NrStage::BeforeUpscale, "Invalid NR stage must fall back to before upscaling");
        std::cout << "Settings migration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
