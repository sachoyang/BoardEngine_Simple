// WinAPI 메시지로부터 마우스 입력 상태를 수집하는 싱글톤 InputManager
#pragma once

class InputManager
{
public:
    static InputManager& Instance();

    // WindowProc 에서 호출
    void OnMouseMove   (int x, int y);
    void OnLButtonDown (int x, int y);
    void OnLButtonUp   (int x, int y);
    void OnRButtonDown (int x, int y);
    void OnRButtonUp   (int x, int y);
    void OnMouseWheel  (int delta);     // WM_MOUSEWHEEL: 양수=위, 음수=아래, 단위=WHEEL_DELTA(120)

    // Render() 끝에서 호출 — 프레임 단위 상태(delta, lPressed, wheelDelta) 초기화
    void EndFrame();

    int  GetMouseX()        const { return m_mouseX; }
    int  GetMouseY()        const { return m_mouseY; }
    int  GetDeltaX()        const { return m_deltaX; }
    int  GetDeltaY()        const { return m_deltaY; }
    int  GetWheelDelta()    const { return m_wheelDelta; }
    bool IsLButtonDown()    const { return m_lDown; }
    bool IsLButtonPressed() const { return m_lPressed; } // 해당 프레임에 처음 눌린 경우만 true
    bool IsRButtonDown()    const { return m_rDown; }

private:
    InputManager() = default;

    int  m_mouseX     = 0;
    int  m_mouseY     = 0;
    int  m_deltaX     = 0;   // 이번 프레임의 누적 X 이동량
    int  m_deltaY     = 0;   // 이번 프레임의 누적 Y 이동량
    int  m_wheelDelta = 0;   // 이번 프레임의 누적 휠 델타
    bool m_lDown      = false;
    bool m_lPressed   = false;
    bool m_rDown      = false;
};
