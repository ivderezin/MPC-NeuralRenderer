// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RuntimeLibrary.h"
#include <ffx/api/include/ffx_api.h>
#include <ffx/api/include/dx12/ffx_api_dx12.h>
#include <vector>
namespace processing {
class FsrApi {
    RuntimeLibrary library_;

  public:
    PfnFfxCreateContext create = nullptr;
    decltype(&ffxDestroyContext) destroy = nullptr;
    PfnFfxConfigure configure = nullptr;
    PfnFfxDispatch dispatch = nullptr;
    PfnFfxQuery query = nullptr;
    void Load(const std::filesystem::path& path) {
        library_.Load(path);
        create = library_.Get<PfnFfxCreateContext>("ffxCreateContext");
        destroy = library_.Get<decltype(destroy)>("ffxDestroyContext");
        configure = library_.Get<PfnFfxConfigure>("ffxConfigure");
        dispatch = library_.Get<PfnFfxDispatch>("ffxDispatch");
        query = library_.Get<PfnFfxQuery>("ffxQuery");
    }
    static void Check(ffxReturnCode_t value, const char* operation) {
        if (value != FFX_API_RETURN_OK)
            throw std::runtime_error(std::string(operation) + ": FSR status " + std::to_string(value));
    }
    uint64_t Fsr3Version(ID3D12Device* device, uint64_t type) {
        uint64_t count = 0;
        ffxQueryDescGetVersions desc{};
        desc.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        desc.createDescType = type;
        desc.device = device;
        desc.outputCount = &count;
        Check(query(nullptr, &desc.header), "Query FSR versions");
        if (!count || count > 64)
            throw std::runtime_error("FSR provider list is empty or invalid");
        std::vector<uint64_t> ids(static_cast<size_t>(count));
        std::vector<const char*> names(static_cast<size_t>(count));
        desc.versionIds = ids.data();
        desc.versionNames = names.data();
        Check(query(nullptr, &desc.header), "Query FSR provider names");
        for (size_t i = 0; i < count; ++i) {
            const std::string name = names[i] ? names[i] : "";
            if (name.find("3.1") != std::string::npos || name.find("FSR3") != std::string::npos ||
                name.find("FSR 3") != std::string::npos)
                return ids[i];
        }
        throw std::runtime_error("Cross-vendor FSR 3 provider not available");
    }
};
}
