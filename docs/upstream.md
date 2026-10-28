# Source provenance

- MPC Video Renderer: https://github.com/Aleksoid1978/VideoRenderer, revision `369af64d001b540f4f7e634fd46189b93ffb4b7d`, GPL-3.0 (see LICENSE.txt; original file notices retained).
- BaseClasses: https://github.com/v0lt/BaseClasses, revision `de6f4a9971efcd4f53b2ef94552914c5af25c638`, vendored under external/BaseClasses; original notices retained.
- MinHook: https://github.com/TsudaKageyu/minhook, revision `c3fcafdc10146beb5919319d0683e44e3c30d537`, vendored under external/minhook; see its LICENSE.txt.
- NVIDIA SDK headers: downloaded separately from NVIDIA/DLSS, revision `9981c6350dc58c0ce21f024fd65e31e851c31689`. Not included in source control. NVIDIA terms apply.

The project uses independent renderer and property-page CLSIDs. The upstream IVideoRenderer ABI and interface IIDs remain unchanged for binary compatibility. All settings live under HKCU\Software\MPC Neural Renderer, independently of the upstream renderer. Window class and parent-window property names are also namespaced.

Renderer CLSID: `3C416620-9628-4C7A-AAA0-6C6D42D2A8B1`.
Main property page CLSID: `0BBF91D4-10CC-4835-AAC4-9C751691EBEB`.
Info property page CLSID: `3AA7E936-522B-4B6C-B625-E65DF67C089F`.

MPC-HC external filter selection reference: https://github.com/clsid2/mpc-hc/blob/develop/src/mpc-hc/FGManager.cpp (preferred filters are included in the graph candidate list independently of the selected built-in renderer). No MPC player settings are changed during registration. Playback with this independent CLSID still needs Windows validation.

Feature 18 contract reference (no source copied): https://github.com/bmitch87/DLSS5VKLayer/blob/main/extracted_pipeline_notes.md. The DLL call route is version-sensitive and must be validated on Windows.

DLSS SR and standalone MFG use NVIDIA's pinned `nvsdk_ngx_helpers_d3d.h` and `nvsdk_ngx_helpers_dlssg_d3d.h`. The host owns presentation and frame pacing; no Streamline proxy swap chain or player-DLL replacement is installed.

The BaseClasses `CCritSec` wrapper has a non-blocking `TryLock` addition for timer-driven presentation on the player's window thread. Class layout and existing locking behavior are unchanged.

FSR headers and redistributable runtimes are fetched from GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, revision `60f4ea81909200d8542eca14dccb2628b763a9a3`. XeSS SR / FG and XeLL are fetched from intel/xess, revision `de0fb9c1c510661c571164e1418ceca8101dab69`. Exact URLs and SHA-256 hashes are recorded in `tools/vendor-sdks.json`; SDK files remain outside source control. AMD and Intel license files accompany the Windows package. FSR selects the cross-vendor FSR 3.1 providers explicitly.

FSR FG uses the public standalone dispatch API with `FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY`. XeSS FG uses Intel's D3D12 proxy swap chain and XeLL, while the existing D3D11 video renderer draws into an offscreen surface. Shared NT-handle textures and fences transfer the completed frame and estimated guidance on the same GPU. The two frame-generation backends are mutually exclusive; selecting XeSS never calls the standalone NGX / FSR generator.

EchoStorm motion estimation: `Source/NeuralRendering/EchoMotionShaders.h` adapts the luma pyramid, global shift, block search, vector median and per-pixel refinement from [Echo-Storm/ls-addon-manager](https://github.com/Echo-Storm/ls-addon-manager/blob/8d3eac351d42480326290b0f8965127a630b44f2/addons/DLSS5NR01/src/engine/flow_estimator.cpp), revision `8d3eac351d42480326290b0f8965127a630b44f2`. Copyright (c) 2026 Echo-Storm and andreiday, MIT; the complete notice is retained in `Source/NeuralRendering/EchoStorm-LICENSE.txt` and copied into Windows packages. The host is an independent D3D11 adaptation; the original D3D12 host, shader cache, HDR conversion, statistics, reuse and optional shape-cost mode are not imported. Inputs use this renderer's existing SDR proxy. EchoStorm is the default for new/missing/invalid settings; an explicitly saved Fast selection is preserved. The full motion path adds local-consensus repair from the same revision of `flow_fix.cpp` (`MotionRepairShaders.h`, independent D3D11 host `MotionRepair.cpp`): tile votes, flat/repeating-patch decisions, and a separate repaired field. The corrected EchoStorm field is shared by NR and DLSS/FSR/XeSS SR/FG; Fast retains its original NR/SR field and gets repair before FG. At 4K the voting bins scale with width because this host retains full-resolution analysis. Matching distrust stays separate from fast-motion SR confidence so camera votes are not suppressed. DLSS SR receives bias-current-colour, FSR receives reactive, and XeSS receives responsive-pixel masks. `Upscaling/MotionAdaptiveShaders.h` adapts the Catmull-Rom/lean pass from `sr_engine.cpp` under the same MIT notice, using this renderer's SDR proxy and range-clamped spatial reconstruction in unreliable/fast areas for every SR provider. EASU, sharpening, edge smoothing, frame reuse and the independent EchoFG synthesizer are not imported.
