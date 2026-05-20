// LuaManager 구현 — sol::state 초기화 및 Transform/GameObject 타입 바인딩
#include "LuaManager.h"
#include "Transform.h"
#include "GameObject.h"

LuaManager& LuaManager::Instance()
{
    static LuaManager instance;
    return instance;
}

void LuaManager::Init()
{
    if (m_initialized) return;
    m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);
    BindTypes();
    m_initialized = true;
}

void LuaManager::Shutdown()
{
    m_lua = sol::state{};
    m_initialized = false;
}

void LuaManager::BindTypes()
{
    m_lua.new_usertype<Transform>("Transform",
        "x",           &Transform::x,
        "y",           &Transform::y,
        "width",       &Transform::width,
        "height",      &Transform::height,
        "rotation",    &Transform::rotation,
        "SetPosition", [](Transform* t, float x, float y) { t->x = x; t->y = y; }
    );

    m_lua.new_usertype<GameObject>("GameObject",
        "name", &GameObject::name
    );
}
