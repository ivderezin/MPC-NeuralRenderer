@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH exit /b 1
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b 1
if not exist _bin\tests mkdir _bin\tests
cl /nologo /std:c++20 /EHsc /W4 /WX tests\FrameHistoryTests.cpp /Fo_bin\tests\FrameHistoryTests.obj /Fe_bin\tests\FrameHistoryTests.exe
if errorlevel 1 exit /b 1
_bin\tests\FrameHistoryTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests\DlssCaptureTests.cpp /Fo_bin\tests\DlssCaptureTests.obj /Fe_bin\tests\DlssCaptureTests.exe
if errorlevel 1 exit /b 1
_bin\tests\DlssCaptureTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX tests\ProcessingPolicyTests.cpp /Fo_bin\tests\ProcessingPolicyTests.obj /Fe_bin\tests\ProcessingPolicyTests.exe
if errorlevel 1 exit /b 1
_bin\tests\ProcessingPolicyTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests\NeuralSettingsTests.cpp Source\NeuralRendering\NeuralSettings.cpp /Fo_bin\tests\ /Fe_bin\tests\NeuralSettingsTests.exe /link advapi32.lib
if errorlevel 1 exit /b 1
_bin\tests\NeuralSettingsTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests\PresentationTimerTests.cpp Source\FrameGeneration\PresentationTimer.cpp /Fo_bin\tests\ /Fe_bin\tests\PresentationTimerTests.exe /link user32.lib winmm.lib
if errorlevel 1 exit /b 1
_bin\tests\PresentationTimerTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests\PresentationThreadTests.cpp Source\FrameGeneration\PresentationThread.cpp /Fo_bin\tests\ /Fe_bin\tests\PresentationThreadTests.exe /link winmm.lib
if errorlevel 1 exit /b 1
_bin\tests\PresentationThreadTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W3 /WX- /DNOMINMAX /ISource\EchoStorm /c Source\EchoStorm\addon\compose11.cpp /Fo_bin\tests\
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /ISource\EchoStorm tests\MotionTests.cpp Source\NeuralRendering\MotionEstimator.cpp Source\NeuralRendering\MotionRepair.cpp /Fo_bin\tests\ /Fe_bin\tests\MotionTests.exe /link _bin\tests\compose11.obj d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
_bin\tests\MotionTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests\ColourTransferTests.cpp Source\Processing\ColourTransfer.cpp /Fo_bin\tests\ /Fe_bin\tests\ColourTransferTests.exe /link d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
_bin\tests\ColourTransferTests.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++20 /EHsc /W3 /WX- /DNOMINMAX /ISource\EchoStorm /c Source\EchoStorm\engine\own_synth.cpp Source\EchoStorm\engine\flow_estimator.cpp Source\EchoStorm\engine\flow_fix.cpp Source\EchoStorm\engine\shader_cache.cpp /Fo_bin\tests\
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests\EchoGenerationTests.cpp Source\FrameGeneration\EchoFrameGeneration.cpp Source\FrameGeneration\EchoGuard.cpp /Fo_bin\tests\ /Fe_bin\tests\EchoGenerationTests.exe /link _bin\tests\own_synth.obj _bin\tests\flow_estimator.obj _bin\tests\flow_fix.obj _bin\tests\shader_cache.obj d3d12.lib dxgi.lib d3dcompiler.lib shell32.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
_bin\tests\EchoGenerationTests.exe
exit /b %errorlevel%
