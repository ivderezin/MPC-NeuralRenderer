// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../Processing/Providers.h"
#include <exception>
namespace upscaling {
inline processing::Upscaler AvailableProvider(processing::Upscaler requested,bool dlssAvailable) {
    return requested==processing::Upscaler::Dlss && !dlssAvailable ? processing::Upscaler::Fsr : requested;
}
// Probe support first, but also recover when an apparently available DLSS
// feature cannot be created (e.g. incompatible runtime/driver/size combination).
template<class Create> processing::Upscaler CreateWithFallback(processing::Upscaler requested,bool dlssAvailable,Create create) {
    const auto provider=AvailableProvider(requested,dlssAvailable);
    if(provider==processing::Upscaler::Off)return provider;
    try {create(provider);return provider;}
    catch(const std::exception&) {
        if(provider!=processing::Upscaler::Dlss)throw;
        create(processing::Upscaler::Fsr);return processing::Upscaler::Fsr;
    }
}
}
