param(
    [Parameter(Mandatory=$true)][string]$Compiler,
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$modelRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'core'
if (-not $OutputDirectory) { $OutputDirectory = Split-Path (Split-Path $modelRoot -Parent) -Parent }
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$sourceNames = @('model_api.cpp','core_model.cpp','shapes.cpp','construction.cpp','services.cpp','document.cpp','layout.cpp','layout_geometry.cpp','worktop.cpp')
$sources = $sourceNames | ForEach-Object { Join-Path $modelRoot $_ }
foreach ($source in $sources) { if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw ('Brak źródła: ' + $source) } }
$beforeHashes = @{}
foreach ($source in $sources) { $beforeHashes[$source] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash }
$dllPath = Join-Path $OutputDirectory 'MebelModel.dll'
$flags = @('-std=c++20','-O2','-Wall','-Wextra','-Wno-missing-field-initializers','-static','-static-libstdc++','-static-libgcc','-DUNICODE','-D_UNICODE')
& $Compiler @flags '-shared' @sources '-o' $dllPath
if ($LASTEXITCODE -ne 0) { throw ('Budowanie biblioteki obliczeniowej nie powiodło się: ' + $LASTEXITCODE) }
foreach ($source in $sources) {
    if ($beforeHashes[$source] -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) { throw ('Źródło zmieniło się podczas kompilacji; przebuduj bibliotekę: ' + $source) }
}
$buildReport = @{ok=$true; implementation='C++20'; sources=$beforeHashes; compiler=$Compiler; dll=$dllPath; sha256=(Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash}
$buildReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'MebelModel.build.json') -Encoding utf8
Get-FileHash -LiteralPath $dllPath -Algorithm SHA256
