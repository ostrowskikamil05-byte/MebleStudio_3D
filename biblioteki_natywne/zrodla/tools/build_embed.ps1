param(
    [Parameter(Mandatory=$true)][string]$Compiler,
    [string]$OutputDirectory = '',
    [switch]$RunTests
)
$ErrorActionPreference = 'Stop'
$nativeRoot = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Podaj pelna sciezke do clang++ LLVM-MinGW x64.' }
if (-not $OutputDirectory) { $OutputDirectory = Split-Path $nativeRoot -Parent }
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$includePath = Join-Path $nativeRoot 'third_party\vulkan\include'
$sourceNames = @('native_embed.cpp', 'scene.cpp', 'renderer_d3d11.cpp', 'renderer_d3d12.cpp', 'renderer_vulkan.cpp')
$sources = $sourceNames | ForEach-Object { Join-Path $nativeRoot ('src\' + $_) }
$flags = @('-std=c++20','-O2','-Wall','-Wextra','-Wno-missing-field-initializers','-static','-static-libstdc++','-static-libgcc','-DUNICODE','-D_UNICODE')
$dllPath = Join-Path $OutputDirectory 'MebelNative3D.dll'
& $Compiler @flags '-shared' ('-I' + $includePath) @sources '-ld3d11' '-ld3d12' '-ldxgi' '-ld3dcompiler_47' '-lgdi32' '-lgdiplus' '-luser32' '-lole32' '-o' $dllPath
if ($LASTEXITCODE -ne 0) { throw "Kompilacja silnika nie powiodla sie: $LASTEXITCODE" }
Get-FileHash -LiteralPath $dllPath -Algorithm SHA256
if ($RunTests) {
    $testExe = Join-Path $OutputDirectory 'test_scene_native.exe'
    $testSources = @((Join-Path $nativeRoot 'tests\test_scene_native.cpp'), (Join-Path $nativeRoot 'src\scene.cpp'))
    & $Compiler @flags ('-I' + (Join-Path $nativeRoot 'src')) @testSources '-o' $testExe
    if ($LASTEXITCODE -ne 0) { throw "Kompilacja testow nie powiodla sie: $LASTEXITCODE" }
    & $testExe (Join-Path $nativeRoot 'tests\fixtures') (Join-Path $OutputDirectory 'native_scene_tests.json')
    if ($LASTEXITCODE -ne 0) { throw "Testy sceny nie przeszly: $LASTEXITCODE" }
}
