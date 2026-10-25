// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace processing {
enum class Upscaler : unsigned {
    Off,
    Dlss,
    Fsr,
    Xess
};
enum class FrameGenerator : unsigned {
    Dlss,
    Fsr,
    Xess
};
inline const wchar_t* Name(Upscaler value) {
    switch (value) {
    case Upscaler::Dlss:
        return L"DLSS SR";
    case Upscaler::Fsr:
        return L"FSR 3.1";
    case Upscaler::Xess:
        return L"XeSS";
    default:
        return L"Off";
    }
}
inline const wchar_t* Name(FrameGenerator value) {
    switch (value) {
    case FrameGenerator::Fsr:
        return L"FSR FG";
    case FrameGenerator::Xess:
        return L"XeSS FG";
    default:
        return L"DLSS MFG";
    }
}
}
