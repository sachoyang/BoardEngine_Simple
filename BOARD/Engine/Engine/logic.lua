-- 원형 궤도 이동 테스트 스크립트
-- transform 전역 변수는 ScriptComponent::Load() 에서 C++ 측에서 주입된다.

local time  = 0.0
local cx    = 400.0   -- 궤도 중심 X (픽셀)
local cy    = 300.0   -- 궤도 중심 Y (픽셀)
local r     = 150.0   -- 궤도 반지름

function onUpdate(dt)
    time = time + dt
    transform:SetPosition(cx + math.cos(time) * r,
                          cy + math.sin(time) * r)
end
