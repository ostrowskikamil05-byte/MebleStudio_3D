param([Parameter(Mandatory=$true)][string]$Glslang)
$ErrorActionPreference='Stop'
$shaderRoot=$PSScriptRoot
if(-not (Test-Path -LiteralPath $Glslang)) { throw 'Provide the path to the official Khronos glslang.exe compiler.' }
foreach($stage in @('vert','frag')) {
    & $Glslang -V --target-env vulkan1.0 -o (Join-Path $shaderRoot "shader.$stage.spv") (Join-Path $shaderRoot "shader.$stage")
    if($LASTEXITCODE -ne 0) { throw "glslang compilation failed: $stage" }
}
$lines=[System.Collections.Generic.List[string]]::new()
$lines.Add('// Generated from GLSL by Khronos glslang; Vulkan1.0 SPIR-V.')
$lines.Add('#pragma once')
$lines.Add('#include <cstdint>')
$lines.Add('namespace mebel {')
foreach($item in @(@('vert','vulkanVertexShader'),@('frag','vulkanFragmentShader'))) {
    $bytes=[System.IO.File]::ReadAllBytes((Join-Path $shaderRoot "shader.$($item[0]).spv"))
    if(($bytes.Length % 4) -ne 0) { throw 'Invalid SPIR-V length.' }
    $lines.Add("inline constexpr std::uint32_t $($item[1])[] = {")
    for($offset=0;$offset -lt $bytes.Length;$offset+=32) {
        $words=[System.Collections.Generic.List[string]]::new()
        for($i=$offset;$i -lt [Math]::Min($offset+32,$bytes.Length);$i+=4) {
            $words.Add(('0x{0:x8}u' -f [BitConverter]::ToUInt32($bytes,$i)))
        }
        $lines.Add('    '+($words -join ',')+',')
    }
    $lines.Add('};')
}
$lines.Add('}')
[System.IO.File]::WriteAllLines((Join-Path $shaderRoot 'vulkan_shaders.hpp'),$lines,[System.Text.UTF8Encoding]::new($false))
