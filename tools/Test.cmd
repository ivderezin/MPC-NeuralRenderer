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
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests\MotionTests.cpp /Fo_bin\tests\MotionTests.obj /Fe_bin\tests\MotionTests.exe /link d3d11.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
_bin\tests\MotionTests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX tests\ColourTransferTests.cpp Source\Processing\ColourTransfer.cpp /Fo_bin\tests\ /Fe_bin\tests\ColourTransferTests.exe /link d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
_bin\tests\ColourTransferTests.exe
exit /b %errorlevel%
