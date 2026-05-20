#Requires -Version 5.1
# MakeSDK.ps1 -- BoardEngine SDK extraction script
#
# Prerequisites:
#   Build Engine.sln in Release|x64 first to generate Engine.exe.
#
# Usage:
#   .\MakeSDK.ps1
#   (If execution policy error: Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass)
#
# Output:
#   BoardEngine_SDK/ folder is created at the project root.

[CmdletBinding()]
param()
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# -- Path definitions ----------------------------------------------------------
$ROOT        = $PSScriptRoot
$SRC_ENGINE  = Join-Path $ROOT "Engine\Engine"
$SRC_RELEASE = Join-Path $ROOT "Engine\x64\Release"
$SRC_DLL     = Join-Path $ROOT "Engine\GameLogic"
$SDK         = Join-Path $ROOT "BoardEngine_SDK"

# Public headers to expose in the SDK (no engine implementation .cpp files)
$SDK_HEADERS = @(
    "IEngineAPI.h",
    "IGameLogic.h",
    "GameObject.h",
    "Component.h",
    "Transform.h",
    "SpriteRenderer.h"
)

# -- Utilities -----------------------------------------------------------------
function Write-Step([string]$msg) {
    Write-Host "`n  [$msg]" -ForegroundColor Cyan
}

function Assert-Exists([string]$path, [string]$hint) {
    if (-not (Test-Path $path)) {
        Write-Host "`n  [ERROR] File not found: $path" -ForegroundColor Red
        Write-Host "          $hint"                   -ForegroundColor Yellow
        exit 1
    }
}

# ==============================================================================
Write-Host ""
Write-Host "  BoardEngine SDK Extraction Script" -ForegroundColor Green
Write-Host "  ===================================" -ForegroundColor Green

# -- Step 0: Prerequisites check -----------------------------------------------
Write-Step "Step 0  Prerequisites check"
Assert-Exists "$SRC_RELEASE\Engine.exe"  "Build Engine.sln in Release|x64 first."
Assert-Exists "$SRC_ENGINE\shaders.hlsl" "Engine/Engine/shaders.hlsl is missing."
Assert-Exists "$SRC_DLL\GameLogic.cpp"   "Engine/GameLogic/GameLogic.cpp is missing."
Write-Host "     OK -- All required files found."

# -- Step 1: Initialize SDK folder ---------------------------------------------
Write-Step "Step 1  Initialize SDK folder"
if (Test-Path $SDK) {
    Remove-Item $SDK -Recurse -Force
    Write-Host "     Removed existing BoardEngine_SDK/"
}
New-Item -ItemType Directory -Path "$SDK\include\nlohmann" | Out-Null
New-Item -ItemType Directory -Path "$SDK\GameLogic"        | Out-Null
New-Item -ItemType Directory -Path "$SDK\assets"           | Out-Null
Write-Host "     Folder structure created."

# -- Step 2: Engine binary -----------------------------------------------------
Write-Step "Step 2  Copy engine binary"
Copy-Item "$SRC_RELEASE\Engine.exe" "$SDK\Engine.exe"
Write-Host "     Engine.exe        -> SDK/"

# -- Step 3: shaders.hlsl ------------------------------------------------------
Write-Step "Step 3  Copy shader"
Copy-Item "$SRC_ENGINE\shaders.hlsl" "$SDK\shaders.hlsl"
Write-Host "     shaders.hlsl      -> SDK/"

# -- Step 4: assets/ -----------------------------------------------------------
Write-Step "Step 4  Copy assets"
$assetsDir = "$SRC_ENGINE\assets"
if (Test-Path $assetsDir) {
    Copy-Item "$assetsDir\*" "$SDK\assets" -Recurse -Force
    $count = (Get-ChildItem "$SDK\assets" -Recurse -File).Count
    Write-Host "     assets/           -> SDK/assets/  ($count files)"
} else {
    Write-Host "     [WARN] assets/ folder not found -- copy manually later." -ForegroundColor Yellow
}

# -- Step 5: Public headers ----------------------------------------------------
Write-Step "Step 5  Copy public headers (include/)"
foreach ($h in $SDK_HEADERS) {
    $src = "$SRC_ENGINE\$h"
    if (Test-Path $src) {
        Copy-Item $src "$SDK\include\$h"
        Write-Host "     $h"
    } else {
        Write-Host "     [WARN] $h not found -- skipping." -ForegroundColor Yellow
    }
}
# nlohmann/json.hpp is required by Component.h
$jsonSrc = "$SRC_ENGINE\nlohmann\json.hpp"
if (Test-Path $jsonSrc) {
    Copy-Item $jsonSrc "$SDK\include\nlohmann\json.hpp"
    Write-Host "     nlohmann/json.hpp  (Component.h dependency)"
}

# -- Step 6: GameLogic source --------------------------------------------------
Write-Step "Step 6  Copy GameLogic source"
Copy-Item "$SRC_DLL\GameLogic.cpp" "$SDK\GameLogic\GameLogic.cpp"
Write-Host "     GameLogic.cpp     -> SDK/GameLogic/"

# -- Step 7: Generate GameLogic_SDK.vcxproj ------------------------------------
Write-Step "Step 7  Generate GameLogic_SDK.vcxproj"

# Single-quoted here-string: PowerShell does NOT expand $(ProjectDir) etc.
# MSBuild macros are preserved as-is.
$vcxproj = @'
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>

  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{D4E5F6A7-B8C9-0123-4567-89ABCDEF0123}</ProjectGuid>
    <RootNamespace>GameLogic_SDK</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />

  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <WholeProgramOptimization>true</WholeProgramOptimization>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings" />
  <ImportGroup Label="Shared" />

  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props"
            Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')"
            Label="LocalAppDataPlatform" />
  </ImportGroup>
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props"
            Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')"
            Label="LocalAppDataPlatform" />
  </ImportGroup>

  <PropertyGroup Label="UserMacros" />

  <!--
    OutDir : GameLogic.dll output to SDK root (next to Engine.exe)
    $(ProjectDir)     = BoardEngine_SDK/GameLogic/
    $(ProjectDir)..\  = BoardEngine_SDK/
  -->
  <PropertyGroup>
    <OutDir>$(ProjectDir)..\</OutDir>
    <IntDir>$(ProjectDir)x64\$(Configuration)\</IntDir>
    <TargetName>GameLogic</TargetName>
  </PropertyGroup>

  <!-- Debug|x64 -->
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <SDLCheck>true</SDLCheck>
      <PreprocessorDefinitions>_DEBUG;_WINDOWS;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
      <!-- /MDd: same Debug DLL CRT as Engine.exe (ABI compatibility) -->
      <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
      <!-- SDK public headers only -- no engine source required -->
      <AdditionalIncludeDirectories>$(ProjectDir)..\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Windows</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
    </Link>
  </ItemDefinitionGroup>

  <!-- Release|x64 -->
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <SDLCheck>true</SDLCheck>
      <PreprocessorDefinitions>NDEBUG;_WINDOWS;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
      <!-- /MD: same Release DLL CRT as Engine.exe (ABI compatibility) -->
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
      <AdditionalIncludeDirectories>$(ProjectDir)..\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Windows</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
    </Link>
  </ItemDefinitionGroup>

  <!-- Headers shown in Solution Explorer -->
  <ItemGroup>
    <ClInclude Include="..\include\IEngineAPI.h" />
    <ClInclude Include="..\include\IGameLogic.h" />
    <ClInclude Include="..\include\GameObject.h" />
    <ClInclude Include="..\include\Component.h" />
    <ClInclude Include="..\include\Transform.h" />
    <ClInclude Include="..\include\SpriteRenderer.h" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="GameLogic.cpp" />
  </ItemGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets" />
</Project>
'@

[System.IO.File]::WriteAllText(
    "$SDK\GameLogic\GameLogic_SDK.vcxproj",
    $vcxproj,
    [System.Text.UTF8Encoding]::new($false)   # UTF-8 without BOM
)
Write-Host "     GameLogic_SDK.vcxproj -> SDK/GameLogic/"

# -- Step 8: Generate GameLogic_SDK.sln ----------------------------------------
Write-Step "Step 8  Generate GameLogic_SDK.sln"

$PROJ_GUID = "D4E5F6A7-B8C9-0123-4567-89ABCDEF0123"
$TYPE_GUID = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942"   # VC++ project type GUID
$t = "`t"    # tab
$n = "`r`n"  # CRLF (required by Visual Studio)

$sln  = $n
$sln += "Microsoft Visual Studio Solution File, Format Version 12.00$n"
$sln += "# Visual Studio Version 17$n"
$sln += "VisualStudioVersion = 17.0.31903.59$n"
$sln += "MinimumVisualStudioVersion = 10.0.40219.1$n"
$sln += "Project(`"{$TYPE_GUID}`") = `"GameLogic_SDK`", `"GameLogic_SDK.vcxproj`", `"{$PROJ_GUID}`"$n"
$sln += "EndProject$n"
$sln += "Global$n"
$sln += "${t}GlobalSection(SolutionConfigurationPlatforms) = preSolution$n"
$sln += "${t}${t}Debug|x64 = Debug|x64$n"
$sln += "${t}${t}Release|x64 = Release|x64$n"
$sln += "${t}EndGlobalSection$n"
$sln += "${t}GlobalSection(ProjectConfigurationPlatforms) = postSolution$n"
$sln += "${t}${t}{$PROJ_GUID}.Debug|x64.ActiveCfg = Debug|x64$n"
$sln += "${t}${t}{$PROJ_GUID}.Debug|x64.Build.0 = Debug|x64$n"
$sln += "${t}${t}{$PROJ_GUID}.Release|x64.ActiveCfg = Release|x64$n"
$sln += "${t}${t}{$PROJ_GUID}.Release|x64.Build.0 = Release|x64$n"
$sln += "${t}EndGlobalSection$n"
$sln += "${t}GlobalSection(SolutionProperties) = preSolution$n"
$sln += "${t}${t}HideSolutionNode = FALSE$n"
$sln += "${t}EndGlobalSection$n"
$sln += "EndGlobal$n"

[System.IO.File]::WriteAllText(
    "$SDK\GameLogic\GameLogic_SDK.sln",
    $sln,
    [System.Text.UTF8Encoding]::new($false)
)
Write-Host "     GameLogic_SDK.sln    -> SDK/GameLogic/"

# -- Step 9: Generate README_SDK.md --------------------------------------------
Write-Step "Step 9  Generate README_SDK.md"

# Written as UTF-8 explicitly so Korean characters survive on any system.
$readme = @'
# BoardEngine SDK -- Creator Quick Start

Edit only GameLogic.cpp to build chess, shogi, gomoku, or any board game.
The engine source is sealed. All you need is in this SDK package.

## File Structure

```
BoardEngine_SDK/
+-- Engine.exe                  <- Runtime executable
+-- GameLogic.dll               <- Your build output goes here automatically
+-- shaders.hlsl                <- REQUIRED at runtime (must stay next to Engine.exe)
+-- assets/                     <- Game assets (scene.json, images, .wav)
+-- include/                    <- Public API headers (read-only)
|   +-- IEngineAPI.h            <- Engine API: Instantiate, Destroy, PlayAudio, etc.
|   +-- IGameLogic.h            <- Interface you implement
|   +-- GameObject.h / Component.h / Transform.h / SpriteRenderer.h
|   +-- nlohmann/json.hpp
+-- GameLogic/
    +-- GameLogic_SDK.sln       <- Open this in Visual Studio 2022
    +-- GameLogic_SDK.vcxproj
    +-- GameLogic.cpp           <- Your game logic goes here
```

## Getting Started

1. Open `GameLogic/GameLogic_SDK.sln` in Visual Studio 2022.
2. Edit `GameLogic.cpp`.
3. Build (Ctrl+Shift+B) -> `GameLogic.dll` is placed next to `Engine.exe` automatically.
4. Run `Engine.exe` -> GameLogic menu -> Load GameLogic.dll -> [Play].

## Core API  (see include/IEngineAPI.h)

| Function                        | Description                        |
|---------------------------------|------------------------------------|
| Instantiate(name, x, y)        | Spawn a new GameObject             |
| AddSpriteRenderer(obj, path)   | Attach a texture to an object      |
| Destroy(obj)                   | Remove object at end of frame      |
| FindObjectByName(name)         | Find object by name                |
| PlayAudio(path)                | Play a .wav file (single channel)  |
| SetSpriteTexture(obj, path)    | Swap texture at runtime            |
| SetGameStatusText(text, sec)   | Show overlay text on screen        |

## Important Notes

- Do NOT modify files inside include/ -- they will be overwritten on the next SDK update.
- Always build for x64 (Win32 is not supported).
- CRT must match: Debug build uses /MDd, Release build uses /MD -- do not mix.
'@

[System.IO.File]::WriteAllText(
    "$SDK\README_SDK.md",
    $readme,
    [System.Text.UTF8Encoding]::new($false)
)
Write-Host "     README_SDK.md         -> SDK/"

# -- Final report --------------------------------------------------------------
Write-Host ""
Write-Host "  ================================================" -ForegroundColor Green
Write-Host "   BoardEngine SDK extraction complete!" -ForegroundColor Green
Write-Host "  ================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Generated structure:"
Write-Host "    BoardEngine_SDK\"
Write-Host "    +-- Engine.exe"
Write-Host "    +-- shaders.hlsl"
Write-Host "    +-- assets\"
Write-Host "    +-- include\          (public headers -- no engine source)"
Write-Host "    +-- GameLogic\"
Write-Host "        +-- GameLogic_SDK.sln     <- Open this in Visual Studio"
Write-Host "        +-- GameLogic_SDK.vcxproj"
Write-Host "        +-- GameLogic.cpp"
Write-Host ""
Write-Host "  Next: open BoardEngine_SDK\GameLogic\GameLogic_SDK.sln" -ForegroundColor Cyan
Write-Host ""
