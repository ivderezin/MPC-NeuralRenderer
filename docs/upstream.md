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
