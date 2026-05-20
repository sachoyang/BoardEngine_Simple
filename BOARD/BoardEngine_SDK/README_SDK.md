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