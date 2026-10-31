// SPDX-License-Identifier: GPL-3.0-or-later
// Included after Gpu12: exercises only the host ABI, not an AMD model.
#include "../Source/NeuralRendering/AmdNr.h"
void TestAmdNr(Gpu12& gpu) {
    wchar_t temp[MAX_PATH]{};const auto n=GetTempPathW(MAX_PATH,temp);
    Require(n && n<MAX_PATH,"AMD test temp unavailable");
    const auto folder=std::filesystem::path(temp)/(L"MPC-AmdNr-tests-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder/L"cache/ComputeCache");
    for(const auto* file:{L"nvngx_dlssnr.dll",L"zluda/nvcuda.dll",L"zluda/zluda_real.dll",L"zluda/nvapi64.dll",L"zluda/nvngx.dll",L"kernels/kernels.txt"}) {
        const auto path=folder/file;std::filesystem::create_directories(path.parent_path());std::ofstream placeholder(path,std::ios::binary);
    }
    std::filesystem::copy_file(processing::ModuleDirectory()/L"AmdNrMock.dll",folder/L"dlss5nr_runtime.dll",std::filesystem::copy_options::overwrite_existing);
    {
        processing::RuntimeLibrary inspect;inspect.Load(folder/L"dlss5nr_runtime.dll");
        const auto value=inspect.Get<unsigned(__cdecl*)(unsigned)>("mpc_test_value");
        const auto mode=inspect.Get<void(__cdecl*)(int)>("mpc_test_mode");
        neural::AmdNr backend;
        Require(!backend.Process(nullptr,nullptr,nullptr,nullptr,nullptr,0,0,50,false),"Uninitialized optional runtime must be inert");
        std::filesystem::remove(folder/L"nvngx_dlssnr.dll");bool rejected=false;
        try{backend.Create(gpu.device.Get(),folder);}catch(const std::runtime_error&){rejected=true;}
        Require(rejected && value(0)==0,"Missing AMD dependencies must fail before entering the runtime");
        {std::ofstream placeholder(folder/L"nvngx_dlssnr.dll",std::ios::binary);}
        backend.Create(gpu.device.Get(),folder);
        Require(value(0)==1 && backend.Status().find(L"starting asynchronously")!=std::wstring::npos,"Async startup must be reported without waiting");
        constexpr unsigned w=128,h=64;
        std::vector<unsigned char> pixels(w*h*4,255);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const auto i=(y*w+x)*4;pixels[i]=static_cast<unsigned char>(x);pixels[i+1]=static_cast<unsigned char>(y);pixels[i+2]=90;
        }
        std::vector<unsigned short> flow(w*h*2,0);for(size_t i=0;i<flow.size();i+=2)flow[i]=0x4800; // 8 source pixels.
        std::vector<float> plane(w*h,.5f);
        auto input=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,pixels.data());
        auto output=gpu.Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto motion=gpu.Texture(w,h,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,flow.data());
        auto depth=gpu.Texture(w,h,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,plane.data());
        Require(backend.Process(gpu.list.Get(),input.Get(),output.Get(),motion.Get(),depth.Get(),w,h,50,true),"Starting runtime must provide a base-frame passthrough");
        rejected=false;
        try{backend.Process(gpu.list.Get(),input.Get(),output.Get(),motion.Get(),depth.Get(),w,h,50,false);}catch(const std::logic_error&){rejected=true;}
        Require(rejected,"Pending runtime work cannot be recorded twice before submission");
        gpu.Finish([&]{backend.Submitted(gpu.queue.Get());});
        Require(gpu.Read(output.Get())==pixels,"Starting AMD backend must preserve the actual source frame");
        Require(value(1)==1 && value(2)==1 && value(3)==1 && value(6)==50 && value(7)==100 && value(8)==1000 && value(9)==1,
                "The host must set nonblocking controls, pixel guidance, intensity and reset correctly");
        backend.Submitted(gpu.queue.Get());Require(value(3)==1,"Unrecorded batches must not notify the runtime");
        mode(2);Require(!backend.Process(gpu.list.Get(),input.Get(),output.Get(),motion.Get(),depth.Get(),w,h,50,false),"Failed cache coverage must retain base processing");
        Require(backend.Status().find(L"cache not covered")!=std::wstring::npos && value(2)==1,"Runtime failure must retain its diagnostic without more GPU recording");
        mode(1);Require(backend.Process(gpu.list.Get(),input.Get(),output.Get(),motion.Get(),depth.Get(),w,h,200,false),"Running runtime must accept another frame");
        gpu.Finish([&]{backend.Submitted(gpu.queue.Get());});
        Require(value(6)==100 && value(7)==200 && value(9)==0 && backend.Status().find(L"updates/s")!=std::wstring::npos,
                "Intensity above 100 must use composition strength and retain continuous history");
        Require(gpu.Read(output.Get())==pixels,"Mock processing must retain correct resource states and pixel data");
        mode(-77);Require(!backend.Process(gpu.list.Get(),input.Get(),output.Get(),motion.Get(),depth.Get(),w,h,200,false),"Rejected frames must not publish unwritten output");
        backend.Submitted(gpu.queue.Get());Require(value(3)==2,"Rejected frames must not create a pending notification");
        Require(backend.Status().find(L"-77")!=std::wstring::npos,"SDK process errors must be visible");
        backend.Reset();Require(value(4)==1 && value(5)==0,"Runtime destruction must follow GPU completion with no ABI violations");
    }
    std::filesystem::remove_all(folder);
    std::cout<<"Optional AMD NR ABI, async passthrough, GPU submission, missing dependencies, runtime failure and shutdown passed (mock + WARP)\n";
}
