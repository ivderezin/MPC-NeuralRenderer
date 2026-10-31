$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio 2022 with C++, MFC and ATL is required.' }
if (!(Test-Path (Join-Path $root 'deps/ngx/lib/nvsdk_ngx_s.lib')) -or !(Test-Path (Join-Path $root 'deps/ngx/include/nvsdk_ngx.h'))) {
    & (Join-Path $PSScriptRoot 'Fetch-NgxSdk.ps1')
}
& (Join-Path $PSScriptRoot 'Fetch-VendorSdks.ps1')
& $msbuild (Join-Path $root 'MpcVideoRenderer.sln') /m /nologo /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'Renderer build failed.' }
& $msbuild (Join-Path $root 'tools/replay/DlssFgReplay.vcxproj') /m /nologo /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'DLSS FG replay build failed.' }
$output = Join-Path $root '_bin/Filter_x64'
& (Join-Path $output 'DLSS-FG-Replay.exe') --self-test
if ($LASTEXITCODE -ne 0) { throw 'DLSS FG replay image I/O and diagnostic checks failed.' }
Copy-Item (Join-Path $root 'tools/replay/Replay-README.md') $output
Copy-Item (Join-Path $root 'README.md'), (Join-Path $root 'LICENSE.txt') $output
Copy-Item (Join-Path $root 'distrib/Install_MPCVR_64.cmd'), (Join-Path $root 'distrib/Uninstall_MPCVR_64.cmd') $output
Copy-Item (Join-Path $root 'deps/vendors/runtime/*.dll') $output
Copy-Item (Join-Path $root 'deps/vendors/licenses/*') $output
$imageOutput = Join-Path $output 'docs/images'
New-Item -ItemType Directory -Force $imageOutput | Out-Null
Copy-Item (Join-Path $root 'docs/images/*.jpg') $imageOutput
Copy-Item (Join-Path $root 'docs/dlss-fg-investigation.md') (Join-Path $output 'docs')
Copy-Item (Join-Path $root 'docs/amd-nr.md') (Join-Path $output 'docs')
Copy-Item (Join-Path $root 'Source/NeuralRendering/AmdRuntime/LICENSE.txt') (Join-Path $output 'AMD-NR-RUNTIME-LICENSE.txt')
$replayDocs = Join-Path $output 'tools/replay'
New-Item -ItemType Directory -Force $replayDocs | Out-Null
Copy-Item (Join-Path $root 'tools/replay/Replay-README.md') $replayDocs
Copy-Item (Join-Path $root 'Source/NeuralRendering/EchoStorm-LICENSE.txt') $output
# The independent NR path loads its own adapter dynamically. Validate the packaged
# binary and its ABI on the runner, even though the NVIDIA model is not installed.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class EchoForwarderPackage {
    [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32", CharSet=CharSet.Ansi, ExactSpelling=true)]
    public static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32")] public static extern bool FreeLibrary(IntPtr module);
}
'@
$forwarder = [EchoForwarderPackage]::LoadLibraryExW((Join-Path $output 'nvngx.dll_mpc_echo_nr.dll'), [IntPtr]::Zero, 0x1100)
if ($forwarder -eq [IntPtr]::Zero) { throw "Echo NR adapter could not load: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
try {
    foreach ($export in @('nrfwd_probe', 'nrfwd_init', 'nrfwd_set_float_slot', 'nrfwd_probe_float', 'nrfwd_get_float', 'nrfwd_create', 'nrfwd_evaluate', 'nrfwd_release', 'nrfwd_last_result')) {
        if ([EchoForwarderPackage]::GetProcAddress($forwarder, $export) -eq [IntPtr]::Zero) { throw "Echo NR adapter export missing: $export" }
    }
} finally { [void][EchoForwarderPackage]::FreeLibrary($forwarder) }
Write-Host "Echo NR adapter load and exports verified."
Write-Host "Renderer ready: $output"
