// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <array>
namespace processing {
inline std::filesystem::path ModuleDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module))
        throw std::runtime_error("Cannot locate renderer module");
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        throw std::runtime_error("Renderer path unavailable");
    return std::filesystem::path(path.data()).parent_path();
}
class RuntimeLibrary {
    HMODULE handle_ = nullptr;

  public:
    RuntimeLibrary() = default;
    RuntimeLibrary(const RuntimeLibrary&) = delete;
    RuntimeLibrary& operator=(const RuntimeLibrary&) = delete;
    ~RuntimeLibrary() {
        if (handle_)
            FreeLibrary(handle_);
    }
    void Load(const std::filesystem::path& path) {
        if (handle_)
            throw std::logic_error("Runtime is already loaded");
        handle_ =
            LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle_)
            throw std::runtime_error("Load " + path.filename().string() + ": Windows error " +
                                     std::to_string(GetLastError()));
    }
    template <class T> T Get(const char* name) const {
        auto address = GetProcAddress(handle_, name);
        if (!address)
            throw std::runtime_error(std::string("Runtime export missing: ") + name);
        return reinterpret_cast<T>(address);
    }
};
inline void CheckHr(HRESULT value, const char* operation) {
    if (FAILED(value))
        throw std::runtime_error(std::string(operation) + ": HRESULT " +
                                 std::to_string(static_cast<unsigned long>(value)));
}
inline void CheckIntel(int value, const char* operation) {
    if (value < 0)
        throw std::runtime_error(std::string(operation) + ": status " + std::to_string(value));
}
}
