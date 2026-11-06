param(
    [string]$Compiler = '',
    [string]$BuildDirectory = '',
    [string]$OutputDirectory = '',
    [string]$RuntimeDirectory = '',
    [string]$Python = ''
)
$ErrorActionPreference = 'Stop'

try {
    if (-not $BuildDirectory) { $BuildDirectory = Join-Path $PSScriptRoot 'build-native' }
    if (-not $OutputDirectory) {
        $OutputDirectory = Join-Path $PSScriptRoot ('dist\MebelStudio3D_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
    }
    if (-not $RuntimeDirectory) { $RuntimeDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) '_internal' }
    if (-not (Test-Path -LiteralPath (Join-Path $RuntimeDirectory 'python313.dll') -PathType Leaf)) {
        throw 'Missing complete bundled Python 3.13 runtime. Specify -RuntimeDirectory.'
    }
    $null = Get-Command cmake -ErrorAction Stop
    $null = Get-Command ctest -ErrorAction Stop
    if (-not $Compiler) {
        $knownCompiler = 'C:\msys64\ucrt64\bin\g++.exe'
        if (Test-Path -LiteralPath $knownCompiler -PathType Leaf) { $Compiler = $knownCompiler }
    }
    if ($Python) {
        $pythonCommand = $Python
        $pythonPrefix = @()
    } else {
        $pythonCommand = 'py'
        $pythonPrefix = @('-3.13')
    }
    & $pythonCommand @pythonPrefix -c 'import sys; assert sys.version_info[:2] == (3, 13)'
    if ($LASTEXITCODE -ne 0) { throw 'A development Python 3.13 interpreter is required.' }
    $configureArguments = @('-S', $PSScriptRoot, '-B', $BuildDirectory, '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON')
    if ($Compiler) {
        $Compiler = (Resolve-Path -LiteralPath $Compiler).Path
        $compilerDirectory = Split-Path $Compiler -Parent
        $env:PATH = $compilerDirectory + [IO.Path]::PathSeparator + $env:PATH
        $ninjaPath = Join-Path $compilerDirectory 'ninja.exe'
        if (-not (Test-Path -LiteralPath $ninjaPath -PathType Leaf)) {
            $ninjaPath = (Get-Command ninja -ErrorAction Stop).Source
        }
        $configureArguments += @('-G', 'Ninja', ('-DCMAKE_CXX_COMPILER=' + $Compiler), ('-DCMAKE_MAKE_PROGRAM=' + $ninjaPath))
        $resourceCompiler = Join-Path $compilerDirectory 'windres.exe'
        if (Test-Path -LiteralPath $resourceCompiler -PathType Leaf) {
            $configureArguments += ('-DCMAKE_RC_COMPILER=' + $resourceCompiler)
        }
    }
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $BuildDirectory --config Release --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
    & ctest --test-dir $BuildDirectory -C Release --output-on-failure --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw 'Native regression tests failed; package was not created.' }
    $binaryDirectory = Join-Path $BuildDirectory 'bin'
    if (Test-Path -LiteralPath (Join-Path $binaryDirectory 'Release\MebelStudio3D.exe') -PathType Leaf) {
        $binaryDirectory = Join-Path $binaryDirectory 'Release'
    }
    & $pythonCommand @pythonPrefix (Join-Path $PSScriptRoot 'zbuduj_paczke.py') --build-dir $binaryDirectory --output $OutputDirectory --runtime $RuntimeDirectory --include-sources
    if ($LASTEXITCODE -ne 0) { throw 'Package assembly failed.' }
    Write-Output ('Ready: ' + (Join-Path $OutputDirectory 'MebelStudio3D.exe'))
    exit 0
} catch {
    Write-Error -Message $_ -ErrorAction Continue
    exit 1
}
