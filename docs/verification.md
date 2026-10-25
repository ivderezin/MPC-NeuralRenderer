# Verification status

Performed locally on macOS:

- FrameHistoryTests compiled with Apple Clang, C++20, warnings as errors; tests passed.
- NeuralPass.cpp and NeuralSettings.cpp cross-compiled for Windows x64 with LLVM-MinGW, C++20, warnings as errors.
- The x86 bypass implementation cross-compiled with LLVM-MinGW.
- MotionTests.cpp cross-compiled and linked into a Windows executable with D3D11 / D3DCompiler. It has not been executed locally.
- Visual Studio project XML parsed; the new translation units are included in the renderer target.

Still requires Windows:

- Full MSVC / MFC / ATL renderer build and resource compilation.
- Execution of the D3D11 WARP motion / crop tests.
- Registration and playback in MPC-BE / MPC-HC.
- Real NGX creation and evaluation with a compatible RTX GPU, NVIDIA driver and user-supplied model.
- Measurement of capture latency, frame rate and visual artifacts with Nintendo Switch input.

GitHub Actions is configured for the full Windows build and WARP tests. No CI run or successful model evaluation is claimed yet.
