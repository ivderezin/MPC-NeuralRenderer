// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <string_view>
#include <vector>
#include <cwctype>
#pragma comment(lib,"version.lib")
namespace processing {
struct LoadedModuleInfo { std::wstring path,version; };
inline std::wstring ModuleFileVersion(const wchar_t* path) {
    DWORD ignored=0;const auto size=GetFileVersionInfoSizeW(path,&ignored);
    if (!size) return L"unknown";
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path,0,size,data.data())) return L"unknown";
    VS_FIXEDFILEINFO* info=nullptr;UINT bytes=0;
    if (!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&bytes) ||
        bytes<sizeof(VS_FIXEDFILEINFO) || info->dwSignature!=0xfeef04bd) return L"unknown";
    return std::to_wstring(HIWORD(info->dwFileVersionMS))+L"."+std::to_wstring(LOWORD(info->dwFileVersionMS))+L"."+
        std::to_wstring(HIWORD(info->dwFileVersionLS))+L"."+std::to_wstring(LOWORD(info->dwFileVersionLS));
}
inline std::vector<LoadedModuleInfo> LoadedModulesMatching(std::wstring_view fragment) {
    auto lower=[](std::wstring s) { for(auto& c:s)c=static_cast<wchar_t>(std::towlower(c));return s; };
    const auto match=lower(std::wstring(fragment));
    HANDLE snapshot=INVALID_HANDLE_VALUE;
    for(unsigned attempt=0;attempt<3;++attempt) {
        snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
        if(snapshot!=INVALID_HANDLE_VALUE || GetLastError()!=ERROR_BAD_LENGTH)break;
    }
    std::vector<LoadedModuleInfo> found;
    if(snapshot==INVALID_HANDLE_VALUE)return found;
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Module32FirstW(snapshot,&entry)) do {
        if(lower(entry.szExePath).find(match)!=std::wstring::npos)
            found.push_back({entry.szExePath,ModuleFileVersion(entry.szExePath)});
    } while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
}
inline std::wstring LoadedDlssgStatus() {
    const auto modules=LoadedModulesMatching(L"dlssg");
    std::wstring text=L"Loaded DLSS-G modules: ";
    if(modules.empty())return text+L"not identified (NGX may use an alias)";
    for(const auto& module:modules) {
        if(text.back()!=L' ')text+=L"; ";
        text+=module.version+L" | "+module.path;
    }
    return text;
}
}
