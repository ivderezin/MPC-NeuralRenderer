// SPDX-License-Identifier: GPL-3.0-or-later
#include "AmdNr.h"
#include "AmdRuntime/dlss5nr.h"
#include "../Processing/RuntimeLibrary.h"
#include <algorithm>
#include <sstream>
namespace neural {
namespace {
std::wstring Text(const char* text,size_t length) {
    const int size=MultiByteToWideChar(CP_UTF8,0,text,static_cast<int>(length),nullptr,0);
    std::wstring result(static_cast<size_t>(size),0);
    MultiByteToWideChar(CP_UTF8,0,text,static_cast<int>(length),result.data(),size);return result;
}
}
struct AmdNr::Impl {
    processing::RuntimeLibrary library;
    dlss5nr_context* context=nullptr;
    decltype(&dlss5nr_create) create=nullptr;
    decltype(&dlss5nr_destroy) destroy=nullptr;
    decltype(&dlss5nr_default_settings) defaults=nullptr;
    decltype(&dlss5nr_set_settings) configure=nullptr;
    decltype(&dlss5nr_process) process=nullptr;
    decltype(&dlss5nr_present) present=nullptr;
    decltype(&dlss5nr_get_status) status=nullptr;
    bool pending=false;
    int intensity=-1;
    unsigned width=0;
    std::wstring failure;
    ~Impl(){if(context && destroy)destroy(context);}
    dlss5nr_status Read() const {
        dlss5nr_status value{};value.struct_size=sizeof(value);status(context,&value);return value;
    }
};
AmdNr::AmdNr()=default;
AmdNr::~AmdNr()=default;
void AmdNr::Reset(){impl_.reset();}
void AmdNr::Create(ID3D12Device* device,const std::filesystem::path& directory) {
    if(impl_)throw std::logic_error("AMD NR already initialized");
    for(const auto* file:{L"dlss5nr_runtime.dll",L"nvngx_dlssnr.dll",L"zluda/nvcuda.dll",L"zluda/zluda_real.dll",L"zluda/nvapi64.dll",L"zluda/nvngx.dll",L"kernels/kernels.txt"})
        if(!std::filesystem::is_regular_file(directory/file))throw std::runtime_error("AMD NR package incomplete: "+std::filesystem::path(file).generic_string());
    if(!std::filesystem::is_directory(directory/L"cache/ComputeCache"))throw std::runtime_error("AMD NR translation cache is missing");
    auto next=std::make_unique<Impl>();next->library.Load(directory/L"dlss5nr_runtime.dll");
    next->create=next->library.Get<decltype(next->create)>("dlss5nr_create");
    next->destroy=next->library.Get<decltype(next->destroy)>("dlss5nr_destroy");
    next->defaults=next->library.Get<decltype(next->defaults)>("dlss5nr_default_settings");
    next->configure=next->library.Get<decltype(next->configure)>("dlss5nr_set_settings");
    next->process=next->library.Get<decltype(next->process)>("dlss5nr_process");
    next->present=next->library.Get<decltype(next->present)>("dlss5nr_present");
    next->status=next->library.Get<decltype(next->status)>("dlss5nr_get_status");
    dlss5nr_create_info info{};info.struct_size=sizeof(info);info.device=device;
    const auto path=directory.wstring();info.runtime_dir=path.c_str();
    const int result=next->create(&info,&next->context);
    if(result || !next->context)throw std::runtime_error("Create AMD NR runtime: "+std::to_string(result));
    impl_=std::move(next);
}
bool AmdNr::Process(ID3D12GraphicsCommandList* list,ID3D12Resource* input,ID3D12Resource* output,
    ID3D12Resource* motion,ID3D12Resource* flatDepth,unsigned width,unsigned height,int intensity,bool reset) {
    if(!impl_ || !list || !input || !output || !motion || !flatDepth || !width || !height)return false;
    auto& p=*impl_;if(p.pending)throw std::logic_error("AMD NR frame must be submitted before reuse");
    if(p.Read().state==DLSS5NR_STATE_FAILED)return false;
    intensity=std::clamp(intensity,0,200);
    if(p.intensity!=intensity || p.width!=width) {
        dlss5nr_settings settings{};settings.struct_size=sizeof(settings);p.defaults(&settings);settings.struct_size=sizeof(settings);
        settings.enabled=intensity>0;settings.intensity=(std::min)(1.f,intensity/100.f);
        settings.detail_strength=(std::max)(1.f,intensity/100.f);
        settings.resolution_scale=std::clamp(1280.f/static_cast<float>(width),.25f,1.f);
        settings.encoding=DLSS5NR_ENCODING_DISPLAY;settings.exposure_mode=DLSS5NR_EXPOSURE_FIXED;settings.exposure=1;
        settings.follow_motion=1;settings.network_history=0;
        settings.wait_for_network=0;settings.wait_inside=0;settings.passes=1;
        p.configure(p.context,&settings);p.intensity=intensity;p.width=width;
    }
    dlss5nr_frame frame{};frame.struct_size=sizeof(frame);frame.command_list=list;
    frame.input=input;frame.input_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    frame.output=output;frame.output_state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    frame.width=frame.depth_width=frame.motion_width=width;frame.height=frame.depth_height=frame.motion_height=height;
    frame.depth=flatDepth;frame.depth_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    frame.motion=motion;frame.motion_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    frame.motion_scale_x=frame.motion_scale_y=1;frame.depth_inverted=1;frame.pre_exposure=1;frame.reset=reset;
    const int result=p.process(p.context,&frame);
    if(result){p.failure=L"AMD NR process failed: "+std::to_wstring(result);return false;}
    p.pending=true;return true;
}
void AmdNr::Submitted(ID3D12CommandQueue* queue) {
    if(impl_ && impl_->pending){impl_->present(impl_->context,queue);impl_->pending=false;}
}
std::wstring AmdNr::Status() const {
    if(!impl_)return L"AMD NR: not initialized";
    if(!impl_->failure.empty())return impl_->failure;
    const auto status=impl_->Read();
    const auto end=std::find(status.message,status.message+sizeof(status.message),'\0');
    std::wostringstream text;text<<L"AMD NR: ";
    if(status.state==DLSS5NR_STATE_FAILED)text<<Text(status.message,static_cast<size_t>(end-status.message));
    else if(status.state==DLSS5NR_STATE_STARTING)text<<L"starting asynchronously";
    else text<<status.network_width<<L"x"<<status.network_height<<L", model "<<status.evaluation_ms<<L" ms, "<<status.evaluations_per_second<<L" updates/s";
    return text.str();
}
}
