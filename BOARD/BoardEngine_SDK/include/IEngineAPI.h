// 게임 로직 DLL이 엔진 핵심 기능(오브젝트 생성/삭제/검색)을 호출하기 위한 순수 가상 인터페이스
// DLL 경계를 넘으므로 엔진 내부 헤더(Engine.h 등)를 포함하지 않는다.
#pragma once
#include <string>

class GameObject; // 전방 선언 — 포인터 전달에만 사용

class IEngineAPI
{
public:
    virtual ~IEngineAPI() = default;

    // 이름과 위치로 새 GameObject를 씬에 생성하고 반환한다.
    // Transform 컴포넌트가 자동으로 추가된다 (width/height 기본값 100).
    // 반환값: 생성된 오브젝트 포인터. MAX_OBJECTS 초과 시 nullptr.
    virtual GameObject* Instantiate(const std::string& name, float x, float y) = 0;

    // 오브젝트에 SpriteRenderer를 추가하고 지정 경로의 텍스처를 바인딩한다.
    // Instantiate 직후 호출하여 렌더링을 활성화한다.
    virtual void AddSpriteRenderer(GameObject* obj, const std::string& texturePath) = 0;

    // 오브젝트를 현재 프레임 끝에 안전하게 씬에서 제거한다.
    // 순회 중 즉시 삭제로 인한 크래시를 방지하기 위해 일괄 삭제 방식을 사용한다.
    virtual void Destroy(GameObject* obj) = 0;

    // 이름이 일치하는 첫 번째 오브젝트를 반환한다. 없으면 nullptr.
    virtual GameObject* FindObjectByName(const std::string& name) = 0;

    // 지정 경로의 .wav 파일을 비동기로 재생한다.
    // PlaySoundA(SND_ASYNC | SND_FILENAME) 래퍼 — 동시 재생 불가, 턴제 게임 요건 충족.
    virtual void PlayAudio(const std::string& filePath) = 0;

    // 오브젝트의 SpriteRenderer 텍스처를 실시간으로 교체한다.
    // 기물 승급(폰→퀸), 뒤집기(쇼기), 체크 시 하이라이트 등에 사용한다.
    // texturePath 는 "assets/queen.png" 형식의 상대 경로 또는 절대 경로 모두 허용한다.
    // SpriteRenderer 가 없는 오브젝트에 호출하면 아무 동작도 하지 않는다.
    virtual void SetSpriteTexture(GameObject* obj, const std::string& texturePath) = 0;

    // 화면 상단 중앙에 게임 상태 텍스트를 duration 초 동안 표시한다.
    // "Player 1의 턴", "체크메이트!", "무효한 이동" 등 인게임 알림에 사용한다.
    // 새로운 호출은 이전 텍스트와 타이머를 즉시 교체한다.
    // duration 이 0 이하이면 텍스트를 즉시 지운다.
    virtual void SetGameStatusText(const std::string& text, float duration = 3.0f) = 0;

    // 현재 씬(마지막으로 로드한 scene.json)을 처음 상태로 다시 로드하고
    // 로드된 GameLogic 의 OnLoad() 를 재호출한다 — 보드게임의 "재대국(Rematch)" 한 줄 구현.
    // Godot 의 SceneTree.reload_current_scene() 에서 착안했다.
    // 실제 리로드는 즉시 일어나지 않고 현재 프레임 끝(Deferred)에 안전하게 처리되므로,
    // Update() / OnObjectClicked() 순회 도중에 호출해도 포인터 무효화 크래시가 없다.
    // 따라서 이 호출 직후 objects/clickedObj 포인터를 더 사용하지 말고 즉시 return 하라.
    virtual void RestartGame() = 0;

    // 오브젝트 위치(Transform 중심)에 텍스트를 겹쳐 표시한다 (Label 컴포넌트를 자동 부착).
    // 스프라이트만으로 종류 구분이 어려운 게임에서 사용한다 — 예: 체스 말에 "N"(나이트) 표시.
    // text 가 비어 있으면 라벨을 지운다. (r,g,b) 는 글자 색(0~1, 기본 흰색).
    // 엔진이 글자 밝기에 맞춰 대비 아웃라인을 자동으로 그려 어떤 타일 위에서도 잘 보인다.
    // Unity 의 TextMesh / Godot 의 Label 에서 착안했다.
    virtual void SetObjectText(GameObject* obj, const std::string& text,
                               float r = 1.0f, float g = 1.0f, float b = 1.0f) = 0;
};
