// 오브젝트의 위치·크기·회전을 담당하는 Transform 컴포넌트
#pragma once
#include "Component.h"
#include <DirectXMath.h>

class Transform : public Component
{
public:
    float x        = 0.0f;
    float y        = 0.0f;
    float width    = 100.0f;
    float height   = 100.0f;
    float rotation = 0.0f;   // 라디안

    DirectX::XMMATRIX GetModelMatrix() const;
    DirectX::XMMATRIX GetMVP(const DirectX::XMMATRIX& viewProj) const;

    void ImGuiRender() override;
    const char* GetName() const override { return "Transform"; }

    nlohmann::json Serialize()                    const override;
    void           Deserialize(const nlohmann::json& j) override;
};
