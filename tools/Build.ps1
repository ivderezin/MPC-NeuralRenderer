$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Visual Studio 2022 with C++, MFC and ATL is required.' }
if (!(Test-Path (Join-Path $root 'deps/ngx/lib/nvsdk_ngx_s.lib')) -or !(Test-Path (Join-Path $root 'deps/ngx/include/nvsdk_ngx.h'))) {
    & (Join-Path $PSScriptRoot 'Fetch-NgxSdk.ps1')
}
& $msbuild (Join-Path $root 'MpcVideoRenderer.sln') /m /nologo /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw 'Renderer build failed.' }
$output = Join-Path $root '_bin/Filter_x64'
Copy-Item (Join-Path $root 'README.md'), (Join-Path $root 'LICENSE.txt') $output
Copy-Item (Join-Path $root 'distrib/Install_MPCVR_64.cmd'), (Join-Path $root 'distrib/Uninstall_MPCVR_64.cmd') $output
Write-Host "Renderer ready: $output"
