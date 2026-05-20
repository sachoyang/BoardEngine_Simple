// Lua 스테이트를 소유하고 C++ 타입을 Lua에 바인딩하는 싱글톤 LuaManager
#pragma once
#pragma warning(push, 0)
#define SOL_ALL_SAFETIES_ON 1
#include "sol/sol.hpp"
#pragma warning(pop)

class Transform;
class GameObject;

class LuaManager
{
public:
    static LuaManager& Instance();

    void Init();
    void Shutdown();

    sol::state& GetState() { return m_lua; }

private:
    LuaManager() = default;
    void BindTypes();

    sol::state m_lua;
    bool m_initialized = false;
};
