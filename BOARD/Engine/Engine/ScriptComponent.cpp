// ScriptComponent 구현 — Lua 스크립트 로드 및 onUpdate 콜백 실행
#include <windows.h>
#include "ScriptComponent.h"
#include "LuaManager.h"
#include "Transform.h"
#include "GameObject.h"
#include "imgui.h"

struct ScriptComponent::Impl
{
    sol::protected_function onUpdate;
};

ScriptComponent::ScriptComponent(const std::string& scriptPath)
    : m_scriptPath(scriptPath)
    , m_impl(std::make_unique<Impl>())
{
}

ScriptComponent::~ScriptComponent() = default;

void ScriptComponent::OnAttach()
{
    Load();
}

void ScriptComponent::Load()
{
    auto& lua = LuaManager::Instance().GetState();

    // 스크립트가 접근할 C++ 객체를 Lua 전역 변수로 주입한다.
    // OnAttach() 시점에 gameObject는 이미 설정되어 있다 (AddComponent에서 보장).
    if (gameObject)
    {
        lua["gameObject"] = gameObject;
        auto* tr = gameObject->GetComponent<Transform>();
        if (tr) lua["transform"] = tr;
    }

    auto result = lua.safe_script_file(m_scriptPath, sol::script_pass_on_error);
    if (!result.valid())
    {
        sol::error err = result;
        OutputDebugStringA("[ScriptComponent] 스크립트 오류: ");
        OutputDebugStringA(err.what());
        OutputDebugStringA("\n");
        m_loaded = false;
        return;
    }

    m_impl->onUpdate = lua["onUpdate"];
    m_loaded = m_impl->onUpdate.valid();
}

void ScriptComponent::Update(float dt)
{
    if (!m_loaded) return;
    auto result = m_impl->onUpdate(dt);
    if (!result.valid())
    {
        sol::error err = result;
        OutputDebugStringA("[ScriptComponent] onUpdate 오류: ");
        OutputDebugStringA(err.what());
        OutputDebugStringA("\n");
        m_loaded = false; // 반복 오류 방지
    }
}

void ScriptComponent::Reload()
{
    m_loaded = false;
    Load();
}

void ScriptComponent::ImGuiRender()
{
    ImGui::Text("Script: %s", m_scriptPath.c_str());
    ImGui::Text("Status: %s", m_loaded ? "Loaded" : "Error / Not Loaded");
    if (ImGui::Button("Reload Script"))
        Reload();
}
