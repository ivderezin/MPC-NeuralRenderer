// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only C API implementation. No HIP, CUDA, model or translated kernels.
#define DLSS5NR_BUILD
#include <d3d12.h>
#include "../Source/NeuralRendering/AmdRuntime/dlss5nr.h"
#include <cstring>
#include <thread>
struct dlss5nr_context {
    ID3D12Device* device=nullptr;ID3D12Fence* fence=nullptr;
    dlss5nr_settings settings{};bool configured=false,pending=false;
    unsigned long long submitted=0;
};
static unsigned counts[12]{};
static int mode=DLSS5NR_STATE_STARTING;
extern "C" __declspec(dllexport) unsigned mpc_test_value(unsigned index){return index<12 ? counts[index] : 0;}
extern "C" __declspec(dllexport) void mpc_test_mode(int value){mode=value;}
int dlss5nr_create(const dlss5nr_create_info* info,dlss5nr_context** out) {
    ++counts[0];if(!info || info->struct_size!=sizeof(*info) || !info->device || !info->runtime_dir)return -1;
    auto* c=new dlss5nr_context;c->device=info->device;c->device->AddRef();
    if(FAILED(c->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&c->fence)))){c->device->Release();delete c;return -2;}
    *out=c;return 0;
}
void dlss5nr_default_settings(dlss5nr_settings* s) {
    *s={};s->struct_size=sizeof(*s);s->wait_for_network=s->wait_inside=s->network_history=1;
}
void dlss5nr_set_settings(dlss5nr_context* c,const dlss5nr_settings* s) {
    ++counts[1];c->settings=*s;c->configured=true;
    if(s->struct_size!=sizeof(*s) || s->wait_for_network || s->wait_inside || s->network_history ||
       s->passes!=1 || !s->follow_motion || s->encoding!=DLSS5NR_ENCODING_DISPLAY || s->exposure!=1 ||
       s->resolution_scale<.25f || s->resolution_scale>1)++counts[5];
    counts[6]=static_cast<unsigned>(s->intensity*100);
    counts[7]=static_cast<unsigned>(s->detail_strength*100);
    counts[8]=static_cast<unsigned>(s->resolution_scale*1000);
}
int dlss5nr_process(dlss5nr_context* c,const dlss5nr_frame* f) {
    ++counts[2];if(mode==-77)return -77;
    if(!c->configured || c->pending || f->struct_size!=sizeof(*f) || f->hdr || f->pre_exposure!=1 ||
       !f->depth || !f->motion || f->motion_scale_x!=1 || f->motion_scale_y!=1 || !f->depth_inverted ||
       f->width!=f->motion_width || f->height!=f->motion_height || f->width!=f->depth_width || f->height!=f->depth_height ||
       f->input_state!=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE || f->output_state!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS ||
       f->depth_state!=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE || f->motion_state!=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        ++counts[5];return -2;
    }
    counts[9]=f->reset;counts[10]=f->width;counts[11]=f->height;
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for(auto& b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    barriers[0].Transition={f->input,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    barriers[1].Transition={f->output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};
    f->command_list->ResourceBarrier(2,barriers);f->command_list->CopyResource(f->output,f->input);
    for(auto& b:barriers){const auto state=b.Transition.StateBefore;b.Transition.StateBefore=b.Transition.StateAfter;b.Transition.StateAfter=state;}
    f->command_list->ResourceBarrier(2,barriers);c->pending=true;return 0;
}
void dlss5nr_present(dlss5nr_context* c,ID3D12CommandQueue* queue) {
    ++counts[3];if(!c->pending || !queue){++counts[5];return;}
    queue->Signal(c->fence,++c->submitted);c->pending=false;
}
void dlss5nr_get_status(dlss5nr_context*,dlss5nr_status* s) {
    *s={};s->struct_size=sizeof(*s);s->state=mode;s->network_width=128;s->network_height=64;
    if(mode==DLSS5NR_STATE_FAILED) {
        constexpr char message[]="mock: GPU translation cache not covered";
        std::memcpy(s->message,message,sizeof(message));
    }
}
void dlss5nr_destroy(dlss5nr_context* c) {
    ++counts[4];if(c->pending || c->fence->GetCompletedValue()<c->submitted)++counts[5];
    c->fence->Release();c->device->Release();delete c;
}
uint32_t dlss5nr_loader_thread(dlss5nr_context*){return GetCurrentThreadId();}
int dlss5nr_upscaler_create(const dlss5nr_upscaler_create_info*,dlss5nr_upscaler**){return -1;}
void dlss5nr_upscaler_destroy(dlss5nr_upscaler*){}
int dlss5nr_upscaler_evaluate(dlss5nr_upscaler*,ID3D12CommandQueue*,const dlss5nr_upscaler_frame*){return -1;}
void dlss5nr_upscaler_get_status(dlss5nr_upscaler*,dlss5nr_upscaler_status*){}
const char* dlss5nr_upscaler_last_error(){return "not part of the NR test";}
uint32_t dlss5nr_upscaler_loader_thread(dlss5nr_upscaler*){return 0;}
