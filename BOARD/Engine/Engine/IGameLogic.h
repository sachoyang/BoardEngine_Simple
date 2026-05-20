// 엔진과 GameLogic DLL 사이의 순수 가상 인터페이스
// 이 헤더만 DLL 경계를 넘나든다 — Engine.h 등 무거운 의존성을 포함하지 않는다.
#pragma once
#include <vector>

// 전방 선언: DLL 이 헤더 전체를 몰라도 포인터를 받을 수 있게 한다.
// 실제 타입 정보(vtable, 필드 레이아웃)는 DLL 이 직접 헤더를 포함해서 얻는다.
class GameObject;
class IEngineAPI; // 엔진 스크립팅 API — DLL이 오브젝트 생성/삭제를 요청할 때 사용

class IGameLogic
{
public:
    virtual ~IGameLogic() = default;

    // 씬이 준비되면 한 번 호출된다.
    // api: 엔진 제어권 — Instantiate/Destroy/FindObjectByName 을 호출할 수 있다.
    virtual void OnLoad(std::vector<GameObject*>& objects, IEngineAPI* api) = 0;

    // 매 프레임 호출된다. DLL 은 objects 를 통해 오브젝트를 조작한다.
    virtual void Update(float deltaTime, std::vector<GameObject*>& objects) = 0;

    // DLL 언로드 직전에 호출된다 (내부 리소스 정리).
    virtual void OnUnload() = 0;

    // Play Mode 에서 마우스 좌클릭 피킹 성공 시 호출된다.
    // clickedObj : 클릭된 GameObject (nullptr 이 아님이 보장됨).
    virtual void OnObjectClicked(GameObject* clickedObj) = 0;
};

// DLL 팩토리 함수 포인터 타입 — GetProcAddress 결과 캐스팅에 사용한다.
using CreateGameLogicFn = IGameLogic* (*)();
