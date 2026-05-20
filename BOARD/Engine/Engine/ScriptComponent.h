// Lua 스크립트를 로드하고 매 프레임 onUpdate 함수를 호출하는 ScriptComponent
#pragma once
#include "Component.h"
#include <string>
#include <memory>

class ScriptComponent : public Component
{
public:
    explicit ScriptComponent(const std::string& scriptPath);
    ~ScriptComponent();

    void OnAttach() override;
    void Update(float dt) override;
    void ImGuiRender() override;
    const char* GetName() const override { return "ScriptComponent"; }

    void Reload();

private:
    void Load();

    std::string m_scriptPath;
    bool        m_loaded = false;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
