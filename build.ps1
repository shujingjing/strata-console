$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$vs = $env:STRATA_VCVARS
if (-not $vs) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($install) { $vs = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat" }
  }
}
if (-not $vs -or -not (Test-Path $vs)) {
  throw "Visual Studio C++ build tools not found. Install VS 2022 Build Tools (Desktop development with C++), or set STRATA_VCVARS to vcvars64.bat."
}

$build = Join-Path $root "build"
New-Item -ItemType Directory -Path $build -Force | Out-Null
$src = @(
  (Join-Path $root "src\main.cpp"),
  (Join-Path $root "src\config_store.cpp"),
  (Join-Path $root "src\process.cpp")
) -join '" "'
$rc = Join-Path $root "resources\strata-console.rc"
$res = Join-Path $build "strata-console.res"
$cmd = "call `"$vs`" >nul && rc /nologo /fo `"$res`" /I `"$root\resources`" `"$rc`" && cl /nologo /utf-8 /std:c++17 /O2 /MT /EHsc /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I `"$root\third_party`" /Fo`"$build\\`" `"$src`" `"$res`" /Fe`"$root\strata-console.exe`" /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup ws2_32.lib shell32.lib user32.lib advapi32.lib comdlg32.lib ole32.lib iphlpapi.lib"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "build failed: $LASTEXITCODE" }
Write-Host "built: $root\strata-console.exe"
