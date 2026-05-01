param(
	[string]$ShaderDir = (Join-Path $PSScriptRoot "..\..\data\shader\vulkan"),
	[string]$MirrorDir = (Join-Path $PSScriptRoot "..\..\..\sonic3air\data\shader\vulkan")
)

$ErrorActionPreference = "Stop"

function Get-GlslangValidatorPath {
	if ($env:VULKAN_SDK) {
		$candidate = Join-Path $env:VULKAN_SDK "Bin\glslangValidator.exe"
		if (Test-Path $candidate) {
			return $candidate
		}
	}

	$root = "C:\VulkanSDK"
	if (Test-Path $root) {
		$candidate = Get-ChildItem -Path $root -Directory |
			Sort-Object Name -Descending |
			ForEach-Object { Join-Path $_.FullName "Bin\glslangValidator.exe" } |
			Where-Object { Test-Path $_ } |
			Select-Object -First 1
		if ($candidate) {
			return $candidate
		}
	}

	throw "glslangValidator.exe was not found. Install the Vulkan SDK or set VULKAN_SDK."
}

function Get-ShaderStage([string]$path) {
	$name = [System.IO.Path]::GetFileName($path)
	if ($name.EndsWith(".vert.glsl")) { return "vert" }
	if ($name.EndsWith(".frag.glsl")) { return "frag" }
	throw "Unsupported shader filename '$name'. Expected *.vert.glsl or *.frag.glsl."
}

$glslangValidator = Get-GlslangValidatorPath
$shaderDir = (Resolve-Path $ShaderDir).Path
$shaderFiles = Get-ChildItem -Path $shaderDir -File -Filter *.glsl | Sort-Object Name

if ($shaderFiles.Count -eq 0) {
	throw "No GLSL shader files were found in $shaderDir"
}

foreach ($shaderFile in $shaderFiles) {
	$stage = Get-ShaderStage $shaderFile.FullName
	$outputPath = [System.IO.Path]::ChangeExtension($shaderFile.FullName, ".spv")
	Write-Host "Compiling $($shaderFile.Name) -> $([System.IO.Path]::GetFileName($outputPath))"
	& $glslangValidator -V --target-env vulkan1.3 -S $stage $shaderFile.FullName -o $outputPath
	if ($LASTEXITCODE -ne 0) {
		throw "Failed to compile $($shaderFile.FullName)"
	}
}

if (!(Test-Path $MirrorDir)) {
	New-Item -Path $MirrorDir -ItemType Directory -Force | Out-Null
}

Get-ChildItem -Path $shaderDir -File -Filter *.spv | ForEach-Object {
	$destination = Join-Path $MirrorDir $_.Name
	Copy-Item -Path $_.FullName -Destination $destination -Force
}

Write-Host "Compiled $($shaderFiles.Count) Vulkan shaders."
