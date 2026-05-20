// 모든 컴포넌트의 최상위 추상 클래스 — GameObject에 부착되는 기능 단위
#pragma once
#pragma warning(push, 0)
#include "nlohmann/json.hpp"
#pragma warning(pop)

class GameObject;

class Component
{
public:
    virtual ~Component() = default;

    virtual void OnAttach() {}
    virtual void Update(float dt) {}
    virtual void Render() {}
    virtual void ImGuiRender() {}

    // 컴포넌트 데이터를 JSON 으로 직렬화/역직렬화한다.
    // type 필드 포함 여부: Serialize 는 항상 "type" 키를 포함해야 한다.
    virtual nlohmann::json Serialize() const          { return {}; }
    virtual void Deserialize(const nlohmann::json&)   {}

    virtual const char* GetName() const = 0;

    GameObject* gameObject = nullptr;
};
