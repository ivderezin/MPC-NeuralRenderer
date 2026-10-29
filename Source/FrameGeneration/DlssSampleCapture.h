// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
namespace framegen {
// Optional one-shot capture. Collect only after the host's existing completion
// fence. Destruction/reset also requires completion of recorded GPU work.
class DlssSampleCapture {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    DlssSampleCapture();
    ~DlssSampleCapture();
    static void Request(bool openFolder=true);
    bool Before(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* depth,
                ID3D12Resource* motion,unsigned long long frame,bool reset,std::wstring_view runtime) noexcept;
    void Context(std::string text) noexcept;
    void After(ID3D12GraphicsCommandList* list,const std::array<ID3D12Resource*,3>& outputs,unsigned count) noexcept;
    void Collect(const std::array<unsigned,3>& disabled) noexcept;
    std::wstring Status() const;
    std::filesystem::path Directory() const;
    bool Completed() const;
};
}
