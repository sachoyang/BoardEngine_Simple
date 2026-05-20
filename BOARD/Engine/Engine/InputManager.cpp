// InputManager 구현 — WinAPI 핸들러 및 프레임 단위 상태 리셋
#include "InputManager.h"

InputManager& InputManager::Instance()
{
    static InputManager instance;
    return instance;
}

void InputManager::OnMouseMove(int x, int y)
{
    // 이번 프레임의 총 이동량을 누적한다. Render 호출 전에 여러 WM_MOUSEMOVE 가 올 수 있다.
    m_deltaX += x - m_mouseX;
    m_deltaY += y - m_mouseY;
    m_mouseX  = x;
    m_mouseY  = y;
}

void InputManager::OnLButtonDown(int x, int y)
{
    m_mouseX   = x;
    m_mouseY   = y;
    m_lDown    = true;
    m_lPressed = true;
}

void InputManager::OnLButtonUp(int x, int y)
{
    m_mouseX = x;
    m_mouseY = y;
    m_lDown  = false;
}

void InputManager::OnRButtonDown(int x, int y)
{
    m_mouseX = x;
    m_mouseY = y;
    m_rDown  = true;
}

void InputManager::OnRButtonUp(int x, int y)
{
    m_mouseX = x;
    m_mouseY = y;
    m_rDown  = false;
}

void InputManager::OnMouseWheel(int delta)
{
    m_wheelDelta += delta;
}

void InputManager::EndFrame()
{
    m_deltaX     = 0;
    m_deltaY     = 0;
    m_wheelDelta = 0;
    m_lPressed   = false;
}
