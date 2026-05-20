// D3D12 보드게임 엔진 진입점
#include <windows.h>
#include "Engine.h"
#include "GameObject.h"
#include "Transform.h"
#include "SpriteRenderer.h"
#include "ScriptComponent.h"

static GameObject* MakeSprite(const char* name, float x, float y, float w, float h, float rot = 0.0f)
{
    auto* obj = new GameObject();
    obj->name = name;

    auto* tr = new Transform();
    tr->x = x;  tr->y = y;
    tr->width = w;  tr->height = h;
    tr->rotation = rot;
    obj->AddComponent(tr);

    auto* sr = new SpriteRenderer();
    sr->SetTexture("tile.png"); // 파일이 없으면 체커보드 폴백 텍스처를 사용한다.
    obj->AddComponent(sr);

    return obj;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    Engine gameEngine(hInstance);

    if (!gameEngine.Initialize(800, 600, L"D3D12 Board Game Engine"))
        return -1;

    // Tile A 에 Lua 스크립트를 부착해 원형 궤도 이동 테스트
    auto* tileA = MakeSprite("Tile A", 150.0f, 150.0f, 128.0f, 128.0f);
    tileA->AddComponent(new ScriptComponent("logic.lua"));
    gameEngine.AddGameObject(tileA);

    gameEngine.AddGameObject(MakeSprite("Tile B",   420.0f, 200.0f,  64.0f,  64.0f));
    gameEngine.AddGameObject(MakeSprite("Tile C",   600.0f, 420.0f, 180.0f,  90.0f, 0.5f));

    gameEngine.Run();
    return 0;
}
